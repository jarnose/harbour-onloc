#pragma once

#include "../api/onlocclient.h"
#include "../shared/daemonconfig.h"

#include <QObject>

class CommandHandler;
class LocationReporter;
class QFileSystemWatcher;
class QTimer;
class SocketIoClient;

// The whole background service: reads daemon.json, reports position, listens
// for commands, and writes daemon-status.json for the application to show.
//
// The configuration is watched, so the application changing the interval or
// signing out takes effect at once and the service never needs restarting.
class Daemon : public QObject
{
    Q_OBJECT
public:
    // configPath empty = the default location.
    explicit Daemon(const QString &configPath, QObject *parent = nullptr);

    void setFakePosition(double latitude, double longitude);
    void start();

private:
    void reload();
    void apply();
    void writeStatus();
    void scheduleStatus();

    QString m_configPath;
    DaemonConfig m_config;
    OnlocClient m_client;
    LocationReporter *m_reporter;
    SocketIoClient *m_socket;
    CommandHandler *m_commands;
    QFileSystemWatcher *m_watcher;
    QTimer *m_reloadDebounce;
    QTimer *m_statusDebounce;
    QTimer *m_heartbeat;

    bool m_sessionExpired = false;
    bool m_capabilitiesSent = false;
    QString m_socketKey;   // what the running socket was started with
    QString m_socketError;
};
