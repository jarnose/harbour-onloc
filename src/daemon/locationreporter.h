#pragma once

#include <QByteArray>
#include <QGeoPositionInfo>
#include <QList>
#include <QObject>
#include <QString>

class OnlocClient;
class QGeoPositionInfoSource;
class QTimer;

// Takes fixes at the configured interval and gets them to the server.
//
// Every fix is queued first (in memory and in a JSON-lines file) and uploaded
// from the queue, so a fix taken with no network, or while the server is down,
// is sent when it comes back instead of being lost - with its own timestamp,
// which is why the server's history has the true times even for late uploads.
// The queue is capped; the oldest entries go first.
class LocationReporter : public QObject
{
    Q_OBJECT
public:
    LocationReporter(OnlocClient *client, QObject *parent = nullptr);

    // Starts (or retargets) reporting; interval in seconds.
    void start(const QString &deviceId, int interval);
    void stop();
    bool running() const { return m_running; }

    // For tests on a machine with no positioning: report this place instead.
    void setFakePosition(double latitude, double longitude);

    int queued() const { return m_queue.size(); }
    QString lastUpload() const { return m_lastUpload; }
    QString lastError() const { return m_lastError; }
    // Set once the server said the session is gone; cleared by start().
    bool authFailed() const { return m_authFailed; }

signals:
    void statusChanged();

private:
    void onFix(const QGeoPositionInfo &info);
    void enqueue(const QJsonObject &location);
    void flush();
    void persist();
    void loadQueue();
    void setError(const QString &e);

    OnlocClient *m_client;
    QGeoPositionInfoSource *m_source = nullptr;
    QTimer *m_fakeTimer = nullptr;
    QTimer *m_retry;
    QString m_deviceId;
    int m_interval = 300;
    bool m_running = false;
    bool m_flushing = false;
    bool m_authFailed = false;
    bool m_fake = false;
    double m_fakeLat = 0, m_fakeLon = 0;
    qint64 m_lastFixMs = 0;
    QList<QByteArray> m_queue;   // each a compact JSON location
    QString m_lastUpload;
    QString m_lastError;
    int m_generation = 0;
};
