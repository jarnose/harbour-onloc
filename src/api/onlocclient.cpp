#include "onlocclient.h"

#include <QBuffer>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kTimeoutMs = 20000;

QString messageFrom(const QJsonDocument &doc)
{
    if (doc.isObject())
        return doc.object().value(QStringLiteral("message")).toString();
    return QString();
}
}

OnlocClient::OnlocClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString OnlocClient::normalizeUrl(const QString &input)
{
    QString url = input.trimmed();
    if (url.isEmpty())
        return url;
    if (!url.contains(QStringLiteral("://")))
        url.prepend(QStringLiteral("https://"));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

void OnlocClient::setBaseUrl(const QString &url)
{
    m_baseUrl = normalizeUrl(url);
}

void OnlocClient::clearSession()
{
    m_accessToken.clear();
    m_refreshToken.clear();
}

void OnlocClient::send(const QByteArray &verb, const QString &path, const QUrlQuery &query,
                       const QJsonObject &body, bool hasBody, bool authed, bool retried, Reply cb)
{
    if (m_baseUrl.isEmpty()) {
        cb(0, QJsonDocument(), tr("No server address set"));
        return;
    }

    // An authenticated call with no access token yet (fresh start, or the
    // token was dropped): get one first.
    if (authed && m_accessToken.isEmpty()) {
        if (m_refreshToken.isEmpty()) {
            cb(401, QJsonDocument(), tr("Not signed in"));
            return;
        }
        QPointer<OnlocClient> self(this);
        refreshAccess([=](bool ok, const QString &error) {
            if (!self)
                return;
            if (!ok) {
                // 401 only when the server rejected the session (the refresh
                // token is cleared then); a dead network or a 5xx is status 0
                // so callers retry instead of treating the user as signed out.
                cb(self->m_refreshToken.isEmpty() ? 401 : 0, QJsonDocument(), error);
                return;
            }
            self->send(verb, path, query, body, hasBody, authed, retried, cb);
        });
        return;
    }

    QUrl url(m_baseUrl + path);
    if (!query.isEmpty())
        url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("harbour-onloc"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (authed)
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());

    const QByteArray payload = hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray();
    QNetworkReply *reply = nullptr;
    if (verb == "GET") {
        reply = m_nam->get(req);
    } else if (verb == "POST") {
        reply = m_nam->post(req, payload);
    } else {
        // Qt 5.6's sendCustomRequest takes a QIODevice, not bytes. The buffer
        // is parented to the reply so it lives exactly as long as the request.
        QBuffer *buffer = new QBuffer;
        buffer->setData(payload);
        buffer->open(QIODevice::ReadOnly);
        reply = m_nam->sendCustomRequest(req, verb, buffer);
        buffer->setParent(reply);
    }

    // Qt 5.6 has no QNetworkRequest::setTransferTimeout: abort a stalled
    // request ourselves. abort() makes finished() fire with an error, and
    // the timer dies with the reply.
    QTimer *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(kTimeoutMs);

    QPointer<OnlocClient> self(this);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        if (!self)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (status == 401 && authed && !retried && !m_refreshToken.isEmpty()) {
            m_accessToken.clear();
            refreshAccess([=](bool ok, const QString &error) {
                if (!self)
                    return;
                if (!ok) {
                    cb(self->m_refreshToken.isEmpty() ? 401 : 0, QJsonDocument(), error);
                    return;
                }
                self->send(verb, path, query, body, hasBody, authed, true, cb);
            });
            return;
        }

        if (status >= 200 && status < 300) {
            cb(status, doc, QString());
            return;
        }
        QString error = messageFrom(doc);
        if (error.isEmpty())
            error = status == 0 ? reply->errorString()
                                : tr("Server returned HTTP %1").arg(status);
        cb(status, doc, error);
    });
}

void OnlocClient::refreshAccess(std::function<void(bool, const QString &)> done)
{
    m_refreshWaiters.append(done);
    if (m_refreshing)
        return;
    m_refreshing = true;

    QJsonObject body;
    body.insert(QStringLiteral("refresh_token"), m_refreshToken);
    QPointer<OnlocClient> self(this);
    send("POST", QStringLiteral("/api/auth/refresh"), QUrlQuery(), body, true, false, false,
         [self](int status, const QJsonDocument &doc, const QString &error) {
        if (!self)
            return;
        const QString token = doc.object().value(QStringLiteral("access_token")).toString();
        const bool ok = status == 200 && !token.isEmpty();
        if (ok)
            self->m_accessToken = token;
        // A 400/401/403 means the server understood and said no. Anything
        // else (network down, 5xx) must not throw the session away.
        if (!ok && (status == 400 || status == 401 || status == 403)) {
            self->m_refreshToken.clear();
            emit self->sessionExpired();
        }
        self->m_refreshing = false;
        const auto waiters = self->m_refreshWaiters;
        self->m_refreshWaiters.clear();
        for (const auto &w : waiters)
            w(ok, ok ? QString() : (error.isEmpty() ? tr("Session expired") : error));
    });
}

void OnlocClient::status(Reply cb)
{
    send("GET", QStringLiteral("/api/status"), QUrlQuery(), QJsonObject(), false, false, false, cb);
}

void OnlocClient::login(const QString &username, const QString &password, Reply cb)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    QPointer<OnlocClient> self(this);
    send("POST", QStringLiteral("/api/auth/login"), QUrlQuery(), body, true, false, false,
         [self, cb](int status, const QJsonDocument &doc, const QString &error) {
        if (!self)
            return;
        if (status == 200) {
            const QJsonObject o = doc.object();
            self->m_accessToken = o.value(QStringLiteral("access_token")).toString();
            self->m_refreshToken = o.value(QStringLiteral("refresh_token")).toString();
            emit self->refreshTokenChanged(self->m_refreshToken);
        }
        cb(status, doc, error);
    });
}

void OnlocClient::devices(Reply cb)
{
    send("GET", QStringLiteral("/api/devices"), QUrlQuery(), QJsonObject(), false, true, false, cb);
}

void OnlocClient::sharedDevices(Reply cb)
{
    send("GET", QStringLiteral("/api/devices/shared"), QUrlQuery(), QJsonObject(), false, true, false, cb);
}

void OnlocClient::createDevice(const QString &name, Reply cb)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    send("POST", QStringLiteral("/api/devices"), QUrlQuery(), body, true, true, false, cb);
}

void OnlocClient::locations(const QString &deviceId, const QString &startIso,
                            const QString &endIso, Reply cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("device_id"), deviceId);
    if (!startIso.isEmpty())
        q.addQueryItem(QStringLiteral("start_date"), startIso);
    if (!endIso.isEmpty())
        q.addQueryItem(QStringLiteral("end_date"), endIso);
    send("GET", QStringLiteral("/api/locations"), q, QJsonObject(), false, true, false, cb);
}

void OnlocClient::postLocation(const QJsonObject &location, Reply cb)
{
    // Two server generations disagree on the body: the released API (1.3.0)
    // takes one flat location object, the development branch takes
    // {"locations": [...]} and answers a flat body with 400. Try what worked
    // last (flat at first) and switch once on a 400. The reverse mistake is
    // not safe to probe - the released server reads the array body as an
    // empty location and answers 500.
    const bool flat = m_locationFormat != LocationArray;
    QJsonObject body = location;
    if (!flat)
        body = QJsonObject{{QStringLiteral("locations"), QJsonArray{location}}};
    QPointer<OnlocClient> self(this);
    send("POST", QStringLiteral("/api/locations"), QUrlQuery(), body, true, true, false,
         [self, location, flat, cb](int status, const QJsonDocument &doc, const QString &error) {
        if (!self)
            return;
        if (status == 201) {
            self->m_locationFormat = flat ? LocationFlat : LocationArray;
        } else if (status == 400 && flat) {
            self->m_locationFormat = LocationArray;
            self->postLocation(location, cb);
            return;
        }
        cb(status, doc, error);
    });
}

void OnlocClient::setCapabilities(const QString &deviceId, bool canRing, bool canLock,
                                  bool canFlash, Reply cb)
{
    QJsonObject body;
    body.insert(QStringLiteral("id"), deviceId.toLongLong());
    body.insert(QStringLiteral("can_ring"), canRing);
    body.insert(QStringLiteral("can_lock"), canLock);
    body.insert(QStringLiteral("can_flash"), canFlash);
    send("PATCH", QStringLiteral("/api/devices"), QUrlQuery(), body, true, true, false, cb);
}

void OnlocClient::freshAccessToken(std::function<void(const QString &, const QString &)> cb)
{
    if (m_refreshToken.isEmpty()) {
        cb(QString(), tr("Not signed in"));
        return;
    }
    QPointer<OnlocClient> self(this);
    refreshAccess([self, cb](bool ok, const QString &error) {
        if (!self)
            return;
        cb(ok ? self->m_accessToken : QString(), error);
    });
}

void OnlocClient::ring(const QString &deviceId, Reply cb)
{
    send("POST", QStringLiteral("/api/devices/%1/ring").arg(deviceId), QUrlQuery(),
         QJsonObject(), false, true, false, cb);
}

void OnlocClient::lock(const QString &deviceId, const QString &message, Reply cb)
{
    QJsonObject body;
    if (!message.isEmpty())
        body.insert(QStringLiteral("message"), message);
    send("POST", QStringLiteral("/api/devices/%1/lock").arg(deviceId), QUrlQuery(),
         body, true, true, false, cb);
}

void OnlocClient::flash(const QString &deviceId, Reply cb)
{
    send("POST", QStringLiteral("/api/devices/%1/flash").arg(deviceId), QUrlQuery(),
         QJsonObject(), false, true, false, cb);
}
