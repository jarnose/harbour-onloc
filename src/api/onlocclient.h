#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrlQuery>
#include <functional>

class QNetworkAccessManager;

// Minimal client for the Onloc REST API (onloc-app/onloc-api).
//
// Authentication is a JWT access token (1 h) plus a long-lived refresh token
// (1 y). A request that comes back 401 is retried once after the access token
// has been refreshed; a refresh the server rejects emits sessionExpired() so
// the controller can send the user back to the login page.
//
// Callbacks run once, on this object's thread. They are not called if the
// client is destroyed first.
class OnlocClient : public QObject
{
    Q_OBJECT
public:
    // status is the HTTP status, or 0 when the server could not be reached;
    // error is empty on success.
    using Reply = std::function<void(int status, const QJsonDocument &doc, const QString &error)>;

    explicit OnlocClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    QString baseUrl() const { return m_baseUrl; }
    // Accepts "host", "host:port" or a full URL; adds https:// when there is
    // no scheme and strips a trailing slash.
    static QString normalizeUrl(const QString &input);

    void setRefreshToken(const QString &token) { m_refreshToken = token; }
    QString refreshToken() const { return m_refreshToken; }
    bool hasSession() const { return !m_refreshToken.isEmpty(); }
    void clearSession();

    void status(Reply cb);
    void login(const QString &username, const QString &password, Reply cb);

    void devices(Reply cb);
    void sharedDevices(Reply cb);
    void createDevice(const QString &name, Reply cb);
    void locations(const QString &deviceId, const QString &startIso, const QString &endIso, Reply cb);
    void postLocation(const QJsonObject &location, Reply cb);

    // Tells the server what this device can do (can_ring / can_lock /
    // can_flash). The API takes the whole device row back, so only the
    // flags named here change.
    void setCapabilities(const QString &deviceId, bool canRing, bool canLock, bool canFlash, Reply cb);

    // A fresh access token, always from the refresh token (never the cached
    // one): the Socket.IO connection authenticates once, at connect, and a
    // token that is about to expire is no use for that.
    void freshAccessToken(std::function<void(const QString &token, const QString &error)> cb);

    void ring(const QString &deviceId, Reply cb);
    void lock(const QString &deviceId, const QString &message, Reply cb);
    void flash(const QString &deviceId, Reply cb);

    // Sets the access token; used by login()'s own handler and by tests.
    void setAccessToken(const QString &token) { m_accessToken = token; }

signals:
    // The refresh token was rejected (revoked, or the server was reset).
    void sessionExpired();
    // A new refresh token was issued (login); the controller persists it.
    void refreshTokenChanged(const QString &token);

private:
    void send(const QByteArray &verb, const QString &path, const QUrlQuery &query,
              const QJsonObject &body, bool hasBody, bool authed, bool retried, Reply cb);
    void refreshAccess(std::function<void(bool ok, const QString &error)> done);

    QNetworkAccessManager *m_nam;
    QString m_baseUrl;
    QString m_accessToken;
    QString m_refreshToken;
    // Requests that arrived while a refresh was already in flight.
    QList<std::function<void(bool ok, const QString &error)>> m_refreshWaiters;
    bool m_refreshing = false;
    enum LocationFormat { LocationUnknown, LocationFlat, LocationArray };
    LocationFormat m_locationFormat = LocationUnknown;
};
