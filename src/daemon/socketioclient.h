#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QObject>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// A Socket.IO v4 client over the Engine.IO long-polling transport.
//
// Why polling and not a WebSocket: the SDK target has no QtWebSockets headers
// (the library is on the phone, the development files are not), and a
// WebSocket by hand means a TLS handshake, frame masking and fragmentation for
// the sake of one small feature. Polling is plain HTTP through
// QNetworkAccessManager, the server speaks it by default, and a command that
// arrives within the server's ping interval is delivered at once either way.
//
// What it does: opens a session, connects the default namespace with
// {"token": <JWT>}, answers the server's pings, emits one event on request and
// reports the events it receives. It reconnects with a growing delay on any
// failure and asks for a fresh token each time, because the server checks the
// token only when the connection is made.
class SocketIoClient : public QObject
{
    Q_OBJECT
public:
    using TokenCallback = std::function<void(const QString &token, const QString &error)>;
    using TokenProvider = std::function<void(TokenCallback)>;

    explicit SocketIoClient(QObject *parent = nullptr);

    // Base URL of the server, no trailing slash; Socket.IO's path is /ws.
    void setServer(const QString &baseUrl) { m_server = baseUrl; }
    void setTokenProvider(const TokenProvider &provider) { m_tokenProvider = provider; }
    // Sent as `register-device` every time the namespace connects.
    void setRegisterDevice(const QString &deviceId) { m_deviceId = deviceId; }

    void start();
    void stop();
    bool connected() const { return m_connected; }

signals:
    void connectedChanged(bool connected);
    // An event from the server: name and its first argument (null if none).
    void eventReceived(const QString &name, const QJsonValue &payload);

private:
    QByteArray url(bool withSid) const;
    void connectNow();
    void openSession(const QString &token);
    void sendNamespaceConnect(const QString &token);
    void poll();
    void send(const QByteArray &packet);
    void flush();
    void handlePacket(const QByteArray &packet);
    void failed(const QString &why);
    void setConnected(bool on);
    QNetworkReply *request(const QByteArray &verb, bool withSid, const QByteArray &body);

    QNetworkAccessManager *m_nam;
    QTimer *m_retry;
    QTimer *m_watchdog;
    TokenProvider m_tokenProvider;
    QString m_server;
    QString m_deviceId;

    bool m_running = false;
    bool m_connected = false;
    // Bumped on every (re)connect and on stop(): a reply that comes back for an
    // older session is dropped.
    int m_generation = 0;
    QByteArray m_sid;
    int m_pingInterval = 25000;
    int m_pingTimeout = 20000;
    int m_backoffMs = 2000;

    QList<QByteArray> m_outgoing;
    bool m_posting = false;
};
