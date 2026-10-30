#include "locationreporter.h"

#include "../api/onlocclient.h"
#include "../shared/daemonconfig.h"
#include "../shared/locationjson.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGeoCoordinate>
#include <QGeoPositionInfoSource>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

namespace {
constexpr int kMaxQueue = 5000;
constexpr int kRetryMs = 60000;
}

LocationReporter::LocationReporter(OnlocClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, &LocationReporter::flush);
    loadQueue();
}

void LocationReporter::setFakePosition(double latitude, double longitude)
{
    m_fake = true;
    m_fakeLat = latitude;
    m_fakeLon = longitude;
}

void LocationReporter::start(const QString &deviceId, int interval)
{
    const bool retarget = m_running && (deviceId != m_deviceId || interval != m_interval);
    if (m_running && !retarget)
        return;
    stop();
    m_deviceId = deviceId;
    m_interval = interval;
    m_authFailed = false;
    m_running = true;
    m_lastFixMs = 0;

    if (m_fake) {
        auto tick = [this]() {
            QGeoPositionInfo info(QGeoCoordinate(m_fakeLat, m_fakeLon, 42.0),
                                  QDateTime::currentDateTimeUtc());
            info.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 5.0);
            onFix(info);
        };
        if (!m_fakeTimer) {
            m_fakeTimer = new QTimer(this);
            connect(m_fakeTimer, &QTimer::timeout, this, tick);
        }
        m_fakeTimer->start(m_interval * 1000);
        QTimer::singleShot(0, this, tick);
    } else {
        if (!m_source) {
            m_source = QGeoPositionInfoSource::createDefaultSource(this);
            if (!m_source) {
                setError(QStringLiteral("no positioning source"));
                return;
            }
            connect(m_source, &QGeoPositionInfoSource::positionUpdated, this, &LocationReporter::onFix);
            connect(m_source, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(
                        &QGeoPositionInfoSource::error),
                    this, [this](QGeoPositionInfoSource::Error e) {
                setError(e == QGeoPositionInfoSource::AccessError ? QStringLiteral("location access denied")
                                                                    : QStringLiteral("positioning error"));
            });
        }
        // The source is asked for fixes no faster than we upload them.
        m_source->setUpdateInterval(m_interval * 1000);
        m_source->startUpdates();
    }
    // Anything left over from before (a crash, a restart) goes first.
    flush();
    emit statusChanged();
}

void LocationReporter::stop()
{
    m_running = false;
    ++m_generation;
    m_retry->stop();
    if (m_source)
        m_source->stopUpdates();
    if (m_fakeTimer)
        m_fakeTimer->stop();
    emit statusChanged();
}

void LocationReporter::onFix(const QGeoPositionInfo &info)
{
    if (!m_running || !info.isValid() || !info.coordinate().isValid())
        return;
    // Some sources deliver faster than asked; enforce the interval ourselves.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFixMs && now - m_lastFixMs < qint64(m_interval) * 1000 - 1500)
        return;
    m_lastFixMs = now;
    setError(QString());
    enqueue(locationFromFix(m_deviceId, info));
}

void LocationReporter::enqueue(const QJsonObject &location)
{
    const QByteArray line = QJsonDocument(location).toJson(QJsonDocument::Compact);
    m_queue.append(line);
    bool trimmed = false;
    while (m_queue.size() > kMaxQueue) {
        m_queue.removeFirst();
        trimmed = true;
    }
    if (trimmed) {
        persist();
    } else {
        // Append-only while it only grows; the rewrite happens when it shrinks.
        QDir().mkpath(DaemonConfig::dataDir());
        QFile f(DaemonConfig::queuePath());
        if (f.open(QIODevice::Append)) {
            f.write(line + '\n');
            f.close();
            f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }
    emit statusChanged();
    flush();
}

void LocationReporter::flush()
{
    if (m_flushing || m_queue.isEmpty() || !m_running || m_authFailed)
        return;
    m_flushing = true;
    const int gen = m_generation;
    const QJsonObject location = QJsonDocument::fromJson(m_queue.first()).object();

    QPointer<LocationReporter> self(this);
    m_client->postLocation(location, [self, gen](int status, const QJsonDocument &, const QString &error) {
        if (!self)
            return;
        self->m_flushing = false;
        if (gen != self->m_generation)
            return;
        if (status == 201) {
            self->m_queue.removeFirst();
            self->m_lastUpload = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            self->setError(QString());
            if (self->m_queue.isEmpty())
                self->persist();
            emit self->statusChanged();
            self->flush();
            return;
        }
        if (status == 401) {
            self->m_authFailed = true;
            self->persist();
            self->setError(QStringLiteral("signed out: %1").arg(error));
            return;
        }
        if (status >= 400 && status < 500 && status != 408 && status != 429) {
            // The server will never accept this one (deleted device, bad data):
            // drop it rather than block everything behind it.
            qWarning().noquote() << "dropping a location the server refused:" << status << error;
            self->m_queue.removeFirst();
            self->persist();
            self->setError(error);
            self->flush();
            return;
        }
        // Network down or server trouble: keep everything, try again later.
        self->persist();
        self->setError(error);
        self->m_retry->start();
    });
}

void LocationReporter::persist()
{
    QDir().mkpath(DaemonConfig::dataDir());
    if (m_queue.isEmpty()) {
        QFile::remove(DaemonConfig::queuePath());
        return;
    }
    QSaveFile out(DaemonConfig::queuePath());
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    for (const QByteArray &l : m_queue)
        out.write(l + '\n');
    out.commit();
}

void LocationReporter::loadQueue()
{
    QFile f(DaemonConfig::queuePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (!l.isEmpty() && QJsonDocument::fromJson(l).isObject())
            m_queue.append(l);
    }
    while (m_queue.size() > kMaxQueue)
        m_queue.removeFirst();
}

void LocationReporter::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    if (!e.isEmpty())
        qWarning().noquote() << "reporter:" << e;
    emit statusChanged();
}
