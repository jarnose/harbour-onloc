#include "daemon.h"

#include "commandhandler.h"
#include "locationreporter.h"
#include "socketioclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#ifndef ONLOCD_VERSION
#define ONLOCD_VERSION "1.0.0"
#endif

Daemon::Daemon(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(configPath.isEmpty() ? DaemonConfig::path() : configPath)
    , m_reporter(new LocationReporter(&m_client, this))
    , m_socket(new SocketIoClient(this))
    , m_commands(new CommandHandler(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_reloadDebounce(new QTimer(this))
    , m_statusDebounce(new QTimer(this))
    , m_heartbeat(new QTimer(this))
{
    m_reloadDebounce->setSingleShot(true);
    m_reloadDebounce->setInterval(400);
    connect(m_reloadDebounce, &QTimer::timeout, this, &Daemon::reload);

    m_statusDebounce->setSingleShot(true);
    m_statusDebounce->setInterval(800);
    connect(m_statusDebounce, &QTimer::timeout, this, &Daemon::writeStatus);

    // The status file doubles as a liveness signal for the application, which
    // reads its age: refresh it even when nothing changed.
    m_heartbeat->setInterval(30000);
    connect(m_heartbeat, &QTimer::timeout, this, &Daemon::writeStatus);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        // An atomic save replaces the file, which drops the watch: re-add.
        if (QFileInfo::exists(m_configPath) && !m_watcher->files().contains(m_configPath))
            m_watcher->addPath(m_configPath);
        m_reloadDebounce->start();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        if (QFileInfo::exists(m_configPath) && !m_watcher->files().contains(m_configPath))
            m_watcher->addPath(m_configPath);
        m_reloadDebounce->start();
    });

    connect(&m_client, &OnlocClient::sessionExpired, this, [this]() {
        qWarning() << "session expired: the server rejected the refresh token";
        m_sessionExpired = true;
        m_reporter->stop();
        m_socket->stop();
        m_socketKey.clear();
        scheduleStatus();
    });
    connect(m_reporter, &LocationReporter::statusChanged, this, &Daemon::scheduleStatus);

    connect(m_socket, &SocketIoClient::connectedChanged, this, [this](bool up) {
        qInfo() << "socket:" << (up ? "connected" : "disconnected");
        if (up && !m_capabilitiesSent && m_config.usable()) {
            m_capabilitiesSent = true;
            m_client.setCapabilities(m_config.deviceId, true, true, true,
                                     [](int status, const QJsonDocument &, const QString &error) {
                if (status != 200)
                    qWarning().noquote() << "could not set device capabilities:" << status << error;
                else
                    qInfo() << "device capabilities set";
            });
        }
        scheduleStatus();
    });
    connect(m_socket, &SocketIoClient::eventReceived, this,
            [this](const QString &name, const QJsonValue &payload) {
        if (name == QLatin1String("ring-command"))
            m_commands->ring();
        else if (name == QLatin1String("lock-command"))
            m_commands->lock(payload.toObject().value(QStringLiteral("message")).toString());
        else if (name == QLatin1String("flash-command"))
            m_commands->flash();
        else
            return; // locations-change and the like are for the apps with a UI
        scheduleStatus();
    });

    // Each (re)connect needs a token the server has not seen expire.
    m_socket->setTokenProvider([this](SocketIoClient::TokenCallback cb) {
        m_client.freshAccessToken(cb);
    });
}

void Daemon::setFakePosition(double latitude, double longitude)
{
    m_reporter->setFakePosition(latitude, longitude);
}

void Daemon::start()
{
    const QString dir = QFileInfo(m_configPath).absolutePath();
    QDir().mkpath(dir);
    // Watching the directory too catches the file appearing at all.
    m_watcher->addPath(dir);
    if (QFileInfo::exists(m_configPath))
        m_watcher->addPath(m_configPath);
    m_heartbeat->start();
    reload();
}

void Daemon::reload()
{
    const DaemonConfig fresh = DaemonConfig::load(m_configPath);
    const bool tokenChanged = fresh.refreshToken != m_config.refreshToken || fresh.server != m_config.server;
    m_config = fresh;
    if (tokenChanged)
        m_sessionExpired = false;
    apply();
}

void Daemon::apply()
{
    m_client.setBaseUrl(m_config.server);
    // setRefreshToken keeps a cached access token only if nothing else
    // changed; a new token has to be exchanged afresh.
    if (m_client.refreshToken() != m_config.refreshToken) {
        m_client.setAccessToken(QString());
        m_client.setRefreshToken(m_config.refreshToken);
    }

    const bool live = m_config.usable() && !m_sessionExpired;

    if (live && m_config.enabled)
        m_reporter->start(m_config.deviceId, m_config.interval);
    else
        m_reporter->stop();

    // The socket is restarted only when what it was started with changed.
    const QString key = QStringLiteral("%1|%2|%3").arg(m_config.server, m_config.refreshToken, m_config.deviceId);
    if (live && m_config.commands) {
        if (key != m_socketKey) {
            m_socket->stop();
            m_socket->setServer(m_client.baseUrl());
            m_socket->setRegisterDevice(m_config.deviceId);
            m_socket->start();
            m_socketKey = key;
            m_capabilitiesSent = false;
        }
    } else if (!m_socketKey.isEmpty()) {
        m_socket->stop();
        m_socketKey.clear();
    }
    scheduleStatus();
}

void Daemon::scheduleStatus()
{
    if (!m_statusDebounce->isActive())
        m_statusDebounce->start();
}

void Daemon::writeStatus()
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), QStringLiteral(ONLOCD_VERSION));
    o.insert(QStringLiteral("pid"), qint64(QCoreApplication::applicationPid()));
    o.insert(QStringLiteral("updated"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("configured"), m_config.usable());
    o.insert(QStringLiteral("reporting"), m_reporter->running());
    o.insert(QStringLiteral("socket"), m_socket->connected());
    o.insert(QStringLiteral("queued"), m_reporter->queued());
    o.insert(QStringLiteral("last_upload"), m_reporter->lastUpload());
    o.insert(QStringLiteral("last_error"), m_reporter->lastError());
    o.insert(QStringLiteral("last_command"), m_commands->lastCommand());
    o.insert(QStringLiteral("signed_out"), m_sessionExpired || m_reporter->authFailed());

    QDir().mkpath(DaemonConfig::dataDir());
    QSaveFile out(DaemonConfig::statusPath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        out.commit();
    }
}
