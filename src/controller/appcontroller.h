#pragma once

#include <QObject>
#include <QDateTime>
#include <QGeoPositionInfo>
#include <QString>
#include <QVariantList>

#include "../api/onlocclient.h"
#include "../model/maplistmodel.h"

class QGeoPositionInfoSource;
class QSettings;
class QTimer;
class SecretVault;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    // True while the saved session is being restored at startup, so the UI
    // shows a spinner instead of flashing the login page.
    Q_PROPERTY(bool restoring READ restoring NOTIFY restoringChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString username READ username NOTIFY loggedInChanged)
    Q_PROPERTY(MapListModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(MapListModel *historyModel READ historyModel CONSTANT)
    Q_PROPERTY(bool historyBusy READ historyBusy NOTIFY historyBusyChanged)
    // The loaded history as { latitude, longitude } points, oldest first -
    // what MapCanvas draws.
    Q_PROPERTY(QVariantList historyTrack READ historyTrack NOTIFY historyTrackChanged)

    Q_PROPERTY(bool baseMapEnabled READ baseMapEnabled WRITE setBaseMapEnabled NOTIFY baseMapChanged)
    // Either a TileJSON address or a {z}/{x}/{y} template; see MapTileSource.
    Q_PROPERTY(QString mapTileUrl READ mapTileUrl WRITE setMapTileUrl NOTIFY baseMapChanged)
    Q_PROPERTY(QString defaultMapTileUrl READ defaultMapTileUrl CONSTANT)

    Q_PROPERTY(bool trackingEnabled READ trackingEnabled WRITE setTrackingEnabled NOTIFY trackingChanged)
    Q_PROPERTY(QString thisDeviceId READ thisDeviceId NOTIFY trackingChanged)
    Q_PROPERTY(QString thisDeviceName READ thisDeviceName NOTIFY trackingChanged)
    // Seconds between uploads.
    Q_PROPERTY(int trackingInterval READ trackingInterval WRITE setTrackingInterval NOTIFY trackingChanged)
    Q_PROPERTY(QString trackingStatus READ trackingStatus NOTIFY trackingStatusChanged)

    // The background service (onlocd). Installed = its binary is on the phone;
    // backgroundMode = the user handed it this session (see DaemonConfig).
    Q_PROPERTY(bool daemonInstalled READ daemonInstalled NOTIFY daemonChanged)
    Q_PROPERTY(bool backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY daemonChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonChanged)
    Q_PROPERTY(QVariantMap daemonStatus READ daemonStatus NOTIFY daemonChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool loggedIn() const { return m_loggedIn; }
    bool restoring() const { return m_restoring; }
    bool busy() const { return m_busy > 0; }
    QString serverUrl() const { return m_serverUrl; }
    QString username() const { return m_username; }
    MapListModel *deviceModel() const { return m_devices; }
    MapListModel *historyModel() const { return m_history; }
    bool historyBusy() const { return m_historyBusy; }
    QVariantList historyTrack() const { return m_historyTrack; }

    bool baseMapEnabled() const { return m_baseMapEnabled; }
    void setBaseMapEnabled(bool on);
    QString mapTileUrl() const { return m_mapTileUrl; }
    void setMapTileUrl(const QString &url);
    QString defaultMapTileUrl() const;

    bool trackingEnabled() const { return m_trackingEnabled; }
    void setTrackingEnabled(bool enabled);
    QString thisDeviceId() const { return m_thisDeviceId; }
    QString thisDeviceName() const;
    int trackingInterval() const { return m_trackingInterval; }
    void setTrackingInterval(int seconds);
    QString trackingStatus() const { return m_trackingStatus; }

    bool daemonInstalled() const;
    bool backgroundMode() const { return m_backgroundMode; }
    void setBackgroundMode(bool on);
    bool daemonRunning() const { return m_daemonRunning; }
    QVariantMap daemonStatus() const { return m_daemonStatus; }

    Q_INVOKABLE void login(const QString &serverUrl, const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refreshDevices();
    // Locations of one device from the last `hours` hours, oldest first.
    Q_INVOKABLE void loadHistory(const QString &deviceId, int hours);
    Q_INVOKABLE void clearHistory();

    Q_INVOKABLE void ringDevice(const QString &deviceId);
    Q_INVOKABLE void lockDevice(const QString &deviceId, const QString &message);
    Q_INVOKABLE void flashDevice(const QString &deviceId);

    // Makes an existing device (by id) this phone's tracking identity.
    Q_INVOKABLE void useAsThisDevice(const QString &deviceId);
    // Creates a new device on the server and uses it as this phone.
    Q_INVOKABLE void registerThisDevice(const QString &name);
    Q_INVOKABLE void sendLocationNow();

signals:
    void loggedInChanged();
    void restoringChanged();
    void busyChanged();
    void serverUrlChanged();
    void historyBusyChanged();
    void historyTrackChanged();
    void baseMapChanged();
    void trackingChanged();
    void trackingStatusChanged();
    void daemonChanged();
    void errorOccurred(const QString &message);
    void infoOccurred(const QString &message);
    // Emitted after a successful login so the UI can pop the login page.
    void loginSucceeded();

private slots:
    void onPosition(const QGeoPositionInfo &info);

private:
    void setBusy(bool on);
    void setHistoryTrack(const QVariantList &track);
    void setLoggedIn(bool on);
    void setTrackingStatus(const QString &status);
    void restoreSession();
    void saveRefreshToken(const QString &token);
    void applyDevices(const QJsonArray &own, const QJsonArray &shared);
    void command(const QString &what, const QString &deviceId,
                 const std::function<void(OnlocClient::Reply)> &call);
    bool ensureSource();
    void updateTracking();
    // Writes (or removes) the daemon's config to match the current state.
    void syncDaemonConfig();
    void pollDaemon();
    // True when the daemon, not this process, is doing the reporting.
    bool useDaemon() const;
    void upload(const QGeoPositionInfo &info);
    void setTrackingFrom(const QString &deviceId);

    QSettings *m_settings;
    OnlocClient m_client;
    SecretVault *m_vault;
    MapListModel *m_devices;
    MapListModel *m_history;

    bool m_loggedIn = false;
    bool m_restoring = true;
    int m_busy = 0;
    bool m_historyBusy = false;
    QVariantList m_historyTrack;
    bool m_backgroundMode = false;
    bool m_daemonRunning = false;
    bool m_daemonInstalled = false;
    QVariantMap m_daemonStatus;
    QTimer *m_daemonTimer = nullptr;
    bool m_baseMapEnabled = true;
    QString m_mapTileUrl;
    QString m_serverUrl;
    QString m_username;

    bool m_trackingEnabled = false;
    QString m_thisDeviceId;
    int m_trackingInterval = 300;
    QString m_trackingStatus;
    QGeoPositionInfoSource *m_source = nullptr;
    QDateTime m_lastUpload;
    bool m_uploading = false;
    bool m_forceNext = false;
    // Latest fix kept so "send now" has something to send.
    QGeoPositionInfo m_lastFix;
    // Bumped by logout() so a history/device reply from the previous session
    // is dropped instead of populating the next one.
    int m_generation = 0;
};
