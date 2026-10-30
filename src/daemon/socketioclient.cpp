#include "socketioclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {
const QByteArray kRecordSeparator("\x1e", 1);
constexpr int kMaxBackoffMs = 60000;
}

SocketIoClient::SocketIoClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_retry(new QTimer(this))
    , m_watchdog(new QTimer(this))
{
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, &SocketIoClient::connectNow);

    // The server pings every pingInterval and gives up on us after
    // pingTimeout of silence, so silence on our side for the same total means
    // the session is dead whatever the socket says.
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() { failed(QStringLiteral("no traffic from the server")); });
}

void SocketIoClient::start()
{
    if (m_running)
        return;
    m_running = true;
    m_backoffMs = 2000;
    connectNow();
}

void SocketIoClient::stop()
{
    m_running = false;
    ++m_generation;
    m_retry->stop();
    m_watchdog->stop();
    m_outgoing.clear();
    m_posting = false;
    m_sid.clear();
    setConnected(false);
}

void SocketIoClient::setConnected(bool on)
{
    if (m_connected == on)
        return;
    m_connected = on;
    emit connectedChanged(on);
}

QByteArray SocketIoClient::url(bool withSid) const
{
    QByteArray u = m_server.toUtf8() + "/ws/?EIO=4&transport=polling";
    if (withSid)
        u += "&sid=" + m_sid;
    // Cache-buster: some proxies cache the long GET otherwise.
    u += "&t=" + QByteArray::number(qint64(qrand()), 36);
    return u;
}

QNetworkReply *SocketIoClient::request(const QByteArray &verb, bool withSid, const QByteArray &body)
{
    QNetworkRequest req{QUrl(QString::fromUtf8(url(withSid)))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("onlocd"));
    if (verb == "POST") {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain;charset=UTF-8"));
        return m_nam->post(req, body);
    }
    return m_nam->get(req);
}

void SocketIoClient::connectNow()
{
    if (!m_running)
        return;
    const int gen = ++m_generation;
    m_sid.clear();
    m_outgoing.clear();
    m_posting = false;
    setConnected(false);

    if (!m_tokenProvider) {
        failed(QStringLiteral("no token provider"));
        return;
    }
    QPointer<SocketIoClient> self(this);
    m_tokenProvider([self, gen](const QString &token, const QString &error) {
        if (!self || gen != self->m_generation)
            return;
        if (token.isEmpty()) {
            self->failed(QStringLiteral("no access token: %1").arg(error));
            return;
        }
        self->openSession(token);
    });
}

void SocketIoClient::openSession(const QString &token)
{
    const int gen = m_generation;
    QNetworkReply *reply = request("GET", false, QByteArray());
    QTimer::singleShot(20000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, token]() {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            failed(reply->errorString());
            return;
        }
        const QByteArray body = reply->readAll();
        // 0{"sid":"...","upgrades":[...],"pingInterval":25000,"pingTimeout":20000,...}
        if (!body.startsWith('0')) {
            failed(QStringLiteral("unexpected open packet"));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body.mid(1)).object();
        m_sid = o.value(QStringLiteral("sid")).toString().toUtf8();
        m_pingInterval = o.value(QStringLiteral("pingInterval")).toInt(25000);
        m_pingTimeout = o.value(QStringLiteral("pingTimeout")).toInt(20000);
        if (m_sid.isEmpty()) {
            failed(QStringLiteral("open packet had no session id"));
            return;
        }
        sendNamespaceConnect(token);
        poll();
    });
}

void SocketIoClient::sendNamespaceConnect(const QString &token)
{
    // 4 = Socket.IO message, 0 = CONNECT, then the auth payload.
    send("40" + QJsonDocument(QJsonObject{{QStringLiteral("token"), token}}).toJson(QJsonDocument::Compact));
}

void SocketIoClient::send(const QByteArray &packet)
{
    m_outgoing.append(packet);
    flush();
}

void SocketIoClient::flush()
{
    // Engine.IO polling allows one POST at a time; the rest wait.
    if (m_posting || m_outgoing.isEmpty() || m_sid.isEmpty())
        return;
    m_posting = true;
    // Batch whatever is waiting into one payload.
    QByteArray body = m_outgoing.takeFirst();
    while (!m_outgoing.isEmpty())
        body += kRecordSeparator + m_outgoing.takeFirst();

    const int gen = m_generation;
    QNetworkReply *reply = request("POST", true, body);
    QTimer::singleShot(15000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        m_posting = false;
        if (reply->error() != QNetworkReply::NoError) {
            failed(QStringLiteral("send failed: %1").arg(reply->errorString()));
            return;
        }
        flush();
    });
}

void SocketIoClient::poll()
{
    if (m_sid.isEmpty())
        return;
    const int gen = m_generation;
    // Re-armed on every response; fires only if the server goes quiet.
    m_watchdog->start(m_pingInterval + m_pingTimeout + 5000);

    QNetworkReply *reply = request("GET", true, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            failed(reply->errorString());
            return;
        }
        const QByteArray body = reply->readAll();
        const QList<QByteArray> packets = body.split('\x1e');
        for (const QByteArray &p : packets) {
            if (gen != m_generation)
                return; // a packet reset the session
            if (!p.isEmpty())
                handlePacket(p);
        }
        if (gen == m_generation)
            poll();
    });
}

void SocketIoClient::handlePacket(const QByteArray &packet)
{
    switch (packet.at(0)) {
    case '2': // ping
        send("3");
        return;
    case '1': // close
        failed(QStringLiteral("server closed the session"));
        return;
    case '4': // Socket.IO packet
        break;
    default: // noop, pong, upgrade
        return;
    }
    if (packet.size() < 2)
        return;

    switch (packet.at(1)) {
    case '0': // CONNECT: the namespace is up
        m_backoffMs = 2000;
        setConnected(true);
        if (!m_deviceId.isEmpty()) {
            send("42" + QJsonDocument(QJsonArray{
                    QStringLiteral("register-device"),
                    QJsonObject{{QStringLiteral("device_id"), m_deviceId.toLongLong()}}})
                     .toJson(QJsonDocument::Compact));
        }
        return;
    case '1': // DISCONNECT
        failed(QStringLiteral("server disconnected the socket"));
        return;
    case '4': { // CONNECT_ERROR
        const QString why = QJsonDocument::fromJson(packet.mid(2)).object()
                                    .value(QStringLiteral("message")).toString();
        failed(QStringLiteral("connect refused: %1").arg(why));
        return;
    }
    case '2': { // EVENT: 42["name", arg...]; an ack id may sit before the array
        int i = 2;
        while (i < packet.size() && packet.at(i) >= '0' && packet.at(i) <= '9')
            ++i;
        const QJsonArray a = QJsonDocument::fromJson(packet.mid(i)).array();
        if (a.isEmpty())
            return;
        emit eventReceived(a.at(0).toString(), a.size() > 1 ? a.at(1) : QJsonValue());
        return;
    }
    default:
        return;
    }
}

void SocketIoClient::failed(const QString &why)
{
    if (!m_running)
        return;
    qInfo().noquote() << "socket:" << why << "- retrying in" << m_backoffMs / 1000 << "s";
    ++m_generation; // drop every reply still in flight
    m_watchdog->stop();
    m_sid.clear();
    m_outgoing.clear();
    m_posting = false;
    setConnected(false);
    m_retry->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, kMaxBackoffMs);
}
