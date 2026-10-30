#include "appcontroller.h"

#include "../secrets/secretvault.h"
#include "../shared/daemonconfig.h"
#include "../shared/locationjson.h"

#include <QDir>
#include <QFile>
#include <QGeoCoordinate>
#include <QGeoPositionInfoSource>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
// OpenFreeMap's TileJSON. Not a tile template: the template carries a dated
// path that changes when the planet is rebuilt, so MapTileSource reads the
// current one from this. No account or key; the attribution is drawn on the
// map. See docs/base-map.md.
const QString kDefaultMapTileUrl = QStringLiteral("https://tiles.openfreemap.org/planet");

const QString kRefreshTokenSecret = QStringLiteral("refresh_token");

const QStringList kDeviceKeys = {
    QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("icon"), QStringLiteral("color"),
    QStringLiteral("canRing"), QStringLiteral("canLock"), QStringLiteral("canFlash"),
    QStringLiteral("isConnected"), QStringLiteral("shared"), QStringLiteral("hasLocation"),
    QStringLiteral("latitude"), QStringLiteral("longitude"), QStringLiteral("accuracy"),
    QStringLiteral("battery"), QStringLiteral("charging"), QStringLiteral("updatedAt"),
    QStringLiteral("provider"),
};

const QStringList kHistoryKeys = {
    QStringLiteral("id"), QStringLiteral("latitude"), QStringLiteral("longitude"),
    QStringLiteral("accuracy"), QStringLiteral("altitude"), QStringLiteral("speed"),
    QStringLiteral("bearing"), QStringLiteral("battery"), QStringLiteral("charging"),
    QStringLiteral("provider"), QStringLiteral("createdAt"),
};

// JSON null / missing -> an invalid QVariant, so QML sees `undefined` for
// "the server has no value" rather than a misleading 0.
QVariant optional(const QJsonValue &v)
{
    return v.isNull() || v.isUndefined() ? QVariant() : v.toVariant();
}

QVariantMap locationFields(const QJsonObject &loc)
{
    QVariantMap m;
    m.insert(QStringLiteral("latitude"), loc.value(QStringLiteral("latitude")).toDouble());
    m.insert(QStringLiteral("longitude"), loc.value(QStringLiteral("longitude")).toDouble());
    m.insert(QStringLiteral("accuracy"), optional(loc.value(QStringLiteral("accuracy"))));
    m.insert(QStringLiteral("battery"), optional(loc.value(QStringLiteral("battery"))));
    m.insert(QStringLiteral("charging"), optional(loc.value(QStringLiteral("charging"))));
    m.insert(QStringLiteral("provider"), optional(loc.value(QStringLiteral("provider"))));
    m.insert(QStringLiteral("updatedAt"), loc.value(QStringLiteral("created_at")).toString());
    return m;
}

}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_vault(new SecretVault(this))
    , m_devices(new MapListModel(kDeviceKeys, this))
    , m_history(new MapListModel(kHistoryKeys, this))
{
    // Not a default QSettings: Sailjail only persists the app's own
    // AppConfigLocation, and a default QSettings writes outside it and fails
    // silently.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    m_settings = new QSettings(configDir + QStringLiteral("/settings.ini"), QSettings::IniFormat, this);

    m_serverUrl = m_settings->value(QStringLiteral("server")).toString();
    m_username = m_settings->value(QStringLiteral("username")).toString();
    m_trackingEnabled = m_settings->value(QStringLiteral("tracking/enabled"), false).toBool();
    m_thisDeviceId = m_settings->value(QStringLiteral("tracking/deviceId")).toString();
    m_trackingInterval = qBound(30, m_settings->value(QStringLiteral("tracking/interval"), 300).toInt(), 86400);
    m_backgroundMode = m_settings->value(QStringLiteral("daemon/background"), false).toBool();
    m_baseMapEnabled = m_settings->value(QStringLiteral("map/enabled"), true).toBool();
    m_mapTileUrl = m_settings->value(QStringLiteral("map/tileUrl"), kDefaultMapTileUrl).toString();
    m_client.setBaseUrl(m_serverUrl);

    connect(&m_client, &OnlocClient::sessionExpired, this, [this]() {
        if (!m_loggedIn)
            return;
        logout();
        emit errorOccurred(tr("Session expired - please sign in again"));
    });
    connect(&m_client, &OnlocClient::refreshTokenChanged, this, &AppController::saveRefreshToken);

    // The daemon's status file is small and local; polling it is cheaper than
    // any notification scheme between a sandboxed and an unsandboxed process.
    m_daemonTimer = new QTimer(this);
    m_daemonTimer->setInterval(5000);
    connect(m_daemonTimer, &QTimer::timeout, this, &AppController::pollDaemon);
    m_daemonTimer->start();
    QTimer::singleShot(0, this, &AppController::pollDaemon);

    QTimer::singleShot(0, this, &AppController::restoreSession);
}

AppController::~AppController() = default;

QString AppController::thisDeviceName() const
{
    if (m_thisDeviceId.isEmpty())
        return QString();
    const int row = m_devices->indexOfId(m_thisDeviceId);
    return row >= 0 ? m_devices->get(row).value(QStringLiteral("name")).toString() : QString();
}

void AppController::setBusy(bool on)
{
    const bool was = busy();
    m_busy += on ? 1 : -1;
    if (m_busy < 0)
        m_busy = 0;
    if (was != busy())
        emit busyChanged();
}

void AppController::setLoggedIn(bool on)
{
    if (m_loggedIn == on)
        return;
    m_loggedIn = on;
    emit loggedInChanged();
    updateTracking();
}

void AppController::setTrackingStatus(const QString &status)
{
    if (m_trackingStatus == status)
        return;
    m_trackingStatus = status;
    emit trackingStatusChanged();
}

// --- session ---------------------------------------------------------------

void AppController::restoreSession()
{
    if (m_serverUrl.isEmpty()) {
        m_restoring = false;
        emit restoringChanged();
        return;
    }
    QPointer<AppController> self(this);
    m_vault->ensureCollection([self](bool ok, const QString &) {
        if (!self)
            return;
        if (!ok) {
            self->m_restoring = false;
            emit self->restoringChanged();
            return;
        }
        self->m_vault->loadSecret(kRefreshTokenSecret,
                                  [self](bool found, const QByteArray &data, const QString &) {
            if (!self)
                return;
            if (found && !data.isEmpty()) {
                self->m_client.setRefreshToken(QString::fromUtf8(data));
                self->setLoggedIn(true);
                self->syncDaemonConfig();
                self->refreshDevices();
            }
            self->m_restoring = false;
            emit self->restoringChanged();
        });
    });
}

void AppController::saveRefreshToken(const QString &token)
{
    QPointer<AppController> self(this);
    m_vault->ensureCollection([self, token](bool ok, const QString &error) {
        if (!self)
            return;
        if (!ok) {
            emit self->errorOccurred(tr("Could not open the secret vault: %1").arg(error));
            return;
        }
        self->m_vault->storeSecret(kRefreshTokenSecret, token.toUtf8(),
                                   [self](bool stored, const QString &err) {
            if (self && !stored)
                emit self->errorOccurred(tr("Could not save the session: %1").arg(err));
        });
    });
}

void AppController::login(const QString &serverUrl, const QString &username, const QString &password)
{
    const QString url = OnlocClient::normalizeUrl(serverUrl);
    if (url.isEmpty() || username.isEmpty() || password.isEmpty()) {
        emit errorOccurred(tr("Server, username and password are all required"));
        return;
    }
    m_client.setBaseUrl(url);
    setBusy(true);
    QPointer<AppController> self(this);
    m_client.login(username, password,
                   [self, url, username](int status, const QJsonDocument &, const QString &error) {
        if (!self)
            return;
        self->setBusy(false);
        if (status != 200) {
            emit self->errorOccurred(status == 401 ? tr("Wrong username or password") : error);
            return;
        }
        self->m_serverUrl = url;
        self->m_username = username;
        self->m_settings->setValue(QStringLiteral("server"), url);
        self->m_settings->setValue(QStringLiteral("username"), username);
        emit self->serverUrlChanged();
        self->setLoggedIn(true);
        emit self->loggedInChanged(); // refresh the username binding
        self->syncDaemonConfig();
        emit self->loginSucceeded();
        self->refreshDevices();
    });
}

void AppController::logout()
{
    ++m_generation;
    m_client.clearSession();
    syncDaemonConfig(); // no session left: removes the daemon's copy of it
    m_vault->deleteSecret(kRefreshTokenSecret, [](bool, const QString &) {});
    m_devices->clear();
    m_history->clear();
    setHistoryTrack({});
    m_historyBusy = false;
    emit historyBusyChanged();
    setLoggedIn(false);
}

// --- devices ----------------------------------------------------------------

void AppController::refreshDevices()
{
    if (!m_loggedIn)
        return;
    const int gen = m_generation;
    setBusy(true);
    QPointer<AppController> self(this);
    m_client.devices([self, gen](int status, const QJsonDocument &doc, const QString &error) {
        if (!self || gen != self->m_generation)
            return;
        if (status != 200) {
            self->setBusy(false);
            emit self->errorOccurred(error);
            return;
        }
        const QJsonArray own = doc.object().value(QStringLiteral("devices")).toArray();
        // Shared devices are a bonus: if that call fails (older server, no
        // connections) the list still shows what the user owns.
        self->m_client.sharedDevices([self, gen, own](int, const QJsonDocument &sdoc, const QString &) {
            if (!self || gen != self->m_generation)
                return;
            self->setBusy(false);
            self->applyDevices(own, sdoc.object().value(QStringLiteral("devices")).toArray());
        });
    });
}

void AppController::applyDevices(const QJsonArray &own, const QJsonArray &shared)
{
    QVector<QVariantMap> rows;
    auto add = [&rows](const QJsonValue &v, bool isShared) {
        const QJsonObject d = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("id"), d.value(QStringLiteral("id")).toString());
        m.insert(QStringLiteral("name"), d.value(QStringLiteral("name")).toString());
        m.insert(QStringLiteral("icon"), d.value(QStringLiteral("icon")).toString());
        m.insert(QStringLiteral("color"), d.value(QStringLiteral("color")).toString());
        m.insert(QStringLiteral("isConnected"), d.value(QStringLiteral("is_connected")).toBool());
        m.insert(QStringLiteral("shared"), isShared);

        // On a shared device the owner's capability flags are capped by what
        // the share grants.
        const QJsonObject share = d.value(QStringLiteral("device_share")).toObject();
        auto allowed = [&](const char *key) {
            const bool device = d.value(QLatin1String(key)).toBool();
            return isShared ? device && share.value(QLatin1String(key)).toBool() : device;
        };
        m.insert(QStringLiteral("canRing"), allowed("can_ring"));
        m.insert(QStringLiteral("canLock"), allowed("can_lock"));
        m.insert(QStringLiteral("canFlash"), allowed("can_flash"));

        const QJsonValue latest = d.value(QStringLiteral("latest_location"));
        const bool has = latest.isObject();
        m.insert(QStringLiteral("hasLocation"), has);
        if (has) {
            const QVariantMap f = locationFields(latest.toObject());
            for (auto it = f.begin(); it != f.end(); ++it)
                m.insert(it.key(), it.value());
        }
        rows.append(m);
    };
    for (const QJsonValue &v : own)
        add(v, false);
    for (const QJsonValue &v : shared)
        add(v, true);
    m_devices->setRows(rows);
    emit trackingChanged(); // thisDeviceName may have resolved
}

void AppController::loadHistory(const QString &deviceId, int hours)
{
    if (!m_loggedIn)
        return;
    const int gen = m_generation;
    m_history->clear();
    setHistoryTrack({});
    m_historyBusy = true;
    emit historyBusyChanged();
    const QString start = QDateTime::currentDateTimeUtc().addSecs(-qint64(hours) * 3600)
                              .toString(Qt::ISODate);
    QPointer<AppController> self(this);
    m_client.locations(deviceId, start, QString(),
                       [self, gen](int status, const QJsonDocument &doc, const QString &error) {
        if (!self || gen != self->m_generation)
            return;
        self->m_historyBusy = false;
        emit self->historyBusyChanged();
        if (status != 200) {
            emit self->errorOccurred(error);
            return;
        }
        QVector<QVariantMap> rows;
        const QJsonArray groups = doc.object().value(QStringLiteral("locations")).toArray();
        for (const QJsonValue &g : groups) {
            const QJsonArray locs = g.toObject().value(QStringLiteral("locations")).toArray();
            for (const QJsonValue &lv : locs) {
                const QJsonObject l = lv.toObject();
                QVariantMap m;
                m.insert(QStringLiteral("id"), l.value(QStringLiteral("id")).toString());
                m.insert(QStringLiteral("latitude"), l.value(QStringLiteral("latitude")).toDouble());
                m.insert(QStringLiteral("longitude"), l.value(QStringLiteral("longitude")).toDouble());
                for (const char *key : {"accuracy", "altitude", "speed", "bearing", "battery",
                                        "charging", "provider"})
                    m.insert(QLatin1String(key), optional(l.value(QLatin1String(key))));
                m.insert(QStringLiteral("createdAt"), l.value(QStringLiteral("created_at")).toString());
                rows.append(m);
            }
        }
        // Newest first reads best in a list; the server order is not promised.
        std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
            return QDateTime::fromString(a.value(QStringLiteral("createdAt")).toString(), Qt::ISODate)
                 > QDateTime::fromString(b.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        });
        self->m_history->setRows(rows);

        // Oldest first for the map, which marks the last point as "now".
        QVariantList track;
        for (int i = rows.size() - 1; i >= 0; --i) {
            track.append(QVariantMap{{QStringLiteral("latitude"), rows.at(i).value(QStringLiteral("latitude"))},
                                     {QStringLiteral("longitude"), rows.at(i).value(QStringLiteral("longitude"))}});
        }
        self->m_historyTrack = track;
        emit self->historyTrackChanged();
    });
}

void AppController::clearHistory()
{
    ++m_generation; // drop an in-flight reply for the page that just closed
    m_history->clear();
    setHistoryTrack({});
    if (m_historyBusy) {
        m_historyBusy = false;
        emit historyBusyChanged();
    }
}

void AppController::setHistoryTrack(const QVariantList &track)
{
    if (m_historyTrack.isEmpty() && track.isEmpty())
        return;
    m_historyTrack = track;
    emit historyTrackChanged();
}

// --- background service -------------------------------------------------------

bool AppController::daemonInstalled() const
{
    return DaemonConfig::daemonInstalled();
}

bool AppController::useDaemon() const
{
    return m_backgroundMode && DaemonConfig::daemonInstalled();
}

void AppController::setBackgroundMode(bool on)
{
    if (m_backgroundMode == on)
        return;
    m_backgroundMode = on;
    m_settings->setValue(QStringLiteral("daemon/background"), on);
    syncDaemonConfig();
    updateTracking();
    pollDaemon();
    emit daemonChanged();
}

void AppController::syncDaemonConfig()
{
    const bool want = m_backgroundMode && DaemonConfig::daemonInstalled() && m_loggedIn
            && m_client.hasSession() && !m_thisDeviceId.isEmpty();
    if (!want) {
        // Removing the file is the whole point of switching it off or signing
        // out: the token must not outlive the choice that put it there.
        DaemonConfig::remove();
        return;
    }
    DaemonConfig c;
    c.enabled = m_trackingEnabled;
    c.commands = true;
    c.server = m_serverUrl;
    c.refreshToken = m_client.refreshToken();
    c.deviceId = m_thisDeviceId;
    c.interval = m_trackingInterval;
    if (!c.save())
        emit errorOccurred(tr("Could not write the background service's settings"));
}

void AppController::pollDaemon()
{
    QVariantMap status;
    bool running = false;
    if (useDaemon()) {
        QFile f(DaemonConfig::statusPath());
        if (f.open(QIODevice::ReadOnly)) {
            status = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
            // The daemon rewrites its status at least every 30 s.
            const qint64 updated = status.value(QStringLiteral("updated")).toLongLong();
            running = updated > 0 && QDateTime::currentMSecsSinceEpoch() - updated < 75000;
        }
    }
    const bool installed = DaemonConfig::daemonInstalled();
    if (running == m_daemonRunning && status == m_daemonStatus && installed == m_daemonInstalled)
        return;
    m_daemonInstalled = installed;
    m_daemonRunning = running;
    m_daemonStatus = status;
    emit daemonChanged();

    if (useDaemon() && m_trackingEnabled) {
        if (!running) {
            setTrackingStatus(tr("Background service is not running"));
        } else if (status.value(QStringLiteral("signed_out")).toBool()) {
            setTrackingStatus(tr("Background service was signed out - open the app and sign in again"));
        } else {
            const QString last = status.value(QStringLiteral("last_upload")).toString();
            const QDateTime t = QDateTime::fromString(last, Qt::ISODate);
            QString text = t.isValid()
                    ? tr("Last sent %1").arg(QLocale().toString(t.toLocalTime().time(), QLocale::ShortFormat))
                    : tr("Waiting for a position fix");
            const int queued = status.value(QStringLiteral("queued")).toInt();
            if (queued > 0)
                text += tr(", %1 waiting to be sent").arg(queued);
            setTrackingStatus(text);
        }
    }
}

// --- map --------------------------------------------------------------------

void AppController::setBaseMapEnabled(bool on)
{
    if (m_baseMapEnabled == on)
        return;
    m_baseMapEnabled = on;
    m_settings->setValue(QStringLiteral("map/enabled"), on);
    emit baseMapChanged();
}

void AppController::setMapTileUrl(const QString &url)
{
    // An empty field means "the default", not "no map".
    const QString value = url.trimmed().isEmpty() ? kDefaultMapTileUrl : url.trimmed();
    if (value == m_mapTileUrl)
        return;
    m_mapTileUrl = value;
    m_settings->setValue(QStringLiteral("map/tileUrl"), value);
    emit baseMapChanged();
}

QString AppController::defaultMapTileUrl() const
{
    return kDefaultMapTileUrl;
}

// --- commands ---------------------------------------------------------------

void AppController::command(const QString &what, const QString &deviceId,
                            const std::function<void(OnlocClient::Reply)> &call)
{
    Q_UNUSED(deviceId)
    setBusy(true);
    QPointer<AppController> self(this);
    call([self, what](int status, const QJsonDocument &, const QString &error) {
        if (!self)
            return;
        self->setBusy(false);
        if (status == 200)
            emit self->infoOccurred(tr("%1 sent").arg(what));
        else if (status == 202)
            emit self->infoOccurred(tr("Device is offline - %1 queued until it reconnects").arg(what.toLower()));
        else
            emit self->errorOccurred(error);
    });
}

void AppController::ringDevice(const QString &deviceId)
{
    command(tr("Ring"), deviceId, [=](OnlocClient::Reply cb) { m_client.ring(deviceId, cb); });
}

void AppController::lockDevice(const QString &deviceId, const QString &message)
{
    command(tr("Lock"), deviceId, [=](OnlocClient::Reply cb) { m_client.lock(deviceId, message, cb); });
}

void AppController::flashDevice(const QString &deviceId)
{
    command(tr("Flash"), deviceId, [=](OnlocClient::Reply cb) { m_client.flash(deviceId, cb); });
}

// --- tracking: this phone as a device ---------------------------------------

void AppController::setTrackingFrom(const QString &deviceId)
{
    m_thisDeviceId = deviceId;
    m_settings->setValue(QStringLiteral("tracking/deviceId"), deviceId);
    emit trackingChanged();
    syncDaemonConfig();
    updateTracking();
}

void AppController::useAsThisDevice(const QString &deviceId)
{
    setTrackingFrom(deviceId);
}

void AppController::registerThisDevice(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        emit errorOccurred(tr("Give the device a name"));
        return;
    }
    setBusy(true);
    QPointer<AppController> self(this);
    m_client.createDevice(name.trimmed(), [self](int status, const QJsonDocument &doc, const QString &error) {
        if (!self)
            return;
        self->setBusy(false);
        if (status != 201) {
            emit self->errorOccurred(status == 400 || status == 409 ? tr("Could not create the device: %1").arg(error) : error);
            return;
        }
        const QString id = doc.object().value(QStringLiteral("device")).toObject()
                               .value(QStringLiteral("id")).toString();
        self->refreshDevices();
        if (!id.isEmpty())
            self->setTrackingFrom(id);
    });
}

void AppController::setTrackingEnabled(bool enabled)
{
    if (m_trackingEnabled == enabled)
        return;
    m_trackingEnabled = enabled;
    m_settings->setValue(QStringLiteral("tracking/enabled"), enabled);
    emit trackingChanged();
    syncDaemonConfig();
    updateTracking();
}

void AppController::setTrackingInterval(int seconds)
{
    seconds = qBound(30, seconds, 86400);
    if (m_trackingInterval == seconds)
        return;
    m_trackingInterval = seconds;
    m_settings->setValue(QStringLiteral("tracking/interval"), seconds);
    emit trackingChanged();
    syncDaemonConfig();
}

bool AppController::ensureSource()
{
    if (m_source)
        return true;
    m_source = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_source) {
        setTrackingStatus(tr("No positioning source available"));
        return false;
    }
    connect(m_source, &QGeoPositionInfoSource::positionUpdated, this, &AppController::onPosition);
    connect(m_source, &QGeoPositionInfoSource::updateTimeout, this, [this]() {
        m_forceNext = false;
        setTrackingStatus(tr("No position fix"));
    });
    connect(m_source, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(
                &QGeoPositionInfoSource::error),
            this, [this](QGeoPositionInfoSource::Error e) {
        setTrackingStatus(e == QGeoPositionInfoSource::AccessError
                              ? tr("Location access denied")
                              : tr("Positioning error"));
    });
    m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
    return true;
}

void AppController::updateTracking()
{
    const bool want = m_trackingEnabled && m_loggedIn && !m_thisDeviceId.isEmpty();
    // With the background service holding this session, it does the
    // reporting; running a second source here would upload every fix twice.
    if (want && useDaemon()) {
        if (m_source)
            m_source->stopUpdates();
        pollDaemon();
        return;
    }
    if (!want) {
        if (m_source)
            m_source->stopUpdates();
        setTrackingStatus(m_trackingEnabled ? tr("Waiting for sign-in and a device") : QString());
        return;
    }
    if (!ensureSource())
        return;
    // Ask the source for fixes at the upload interval, not faster: each fix
    // costs power and the upload throttle would throw the extras away.
    m_source->setUpdateInterval(m_trackingInterval * 1000);
    m_source->startUpdates();
    setTrackingStatus(tr("Waiting for a position fix"));
}

void AppController::onPosition(const QGeoPositionInfo &info)
{
    if (!info.isValid() || !info.coordinate().isValid())
        return;
    m_lastFix = info;
    const bool due = !m_lastUpload.isValid()
            || m_lastUpload.secsTo(QDateTime::currentDateTimeUtc()) >= m_trackingInterval - 1;
    if (due || m_forceNext)
        upload(info);
}

void AppController::sendLocationNow()
{
    if (m_thisDeviceId.isEmpty() || !m_loggedIn) {
        emit errorOccurred(tr("Choose or create this device first"));
        return;
    }
    // The upload carries the fix's own timestamp, so a cached fix from the
    // last interval would show up on the server minutes in the past. Reuse
    // one only if it is fresh; otherwise ask for a new one.
    const QDateTime ts = m_lastFix.timestamp();
    if (m_lastFix.isValid() && ts.isValid() && ts.secsTo(QDateTime::currentDateTimeUtc()) < 30) {
        upload(m_lastFix);
        return;
    }
    if (!ensureSource())
        return;
    m_forceNext = true;
    m_source->requestUpdate(30000);
    setTrackingStatus(tr("Waiting for a position fix"));
}

void AppController::upload(const QGeoPositionInfo &info)
{
    if (m_uploading)
        return;
    m_forceNext = false;
    m_uploading = true;

    const QJsonObject loc = locationFromFix(m_thisDeviceId, info);

    const int gen = m_generation;
    QPointer<AppController> self(this);
    m_client.postLocation(loc, [self, gen](int status, const QJsonDocument &, const QString &error) {
        if (!self)
            return;
        self->m_uploading = false;
        if (gen != self->m_generation)
            return;
        if (status == 201) {
            self->m_lastUpload = QDateTime::currentDateTimeUtc();
            self->setTrackingStatus(tr("Last sent %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
            self->refreshDevices();
        } else {
            self->setTrackingStatus(tr("Upload failed: %1").arg(error));
        }
    });
}
