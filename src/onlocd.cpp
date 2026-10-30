// The background service. A systemd user service, packaged separately from
// the application because it cannot run inside the Sailjail sandbox: it has to
// keep running when the application is closed, drive mce and PulseAudio, and
// read a config file the sandboxed application writes. See docs/daemon.md.

#include "daemon/daemon.h"
#include "shared/daemonconfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QDateTime>

namespace {
QFile *g_log = nullptr;

// The journal on this class of device keeps seconds of history, so the
// service keeps its own trail, capped so it cannot grow without bound.
void logHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = type == QtWarningMsg ? "WARN " : type == QtCriticalMsg || type == QtFatalMsg ? "ERROR" : "INFO ";
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODate) + QLatin1Char(' ')
            + QLatin1String(level) + QLatin1Char(' ') + msg + QLatin1Char('\n');
    fputs(qPrintable(line), stderr);
    if (g_log && g_log->isOpen()) {
        if (g_log->size() > 256 * 1024) {
            g_log->close();
            QFile::remove(g_log->fileName() + QStringLiteral(".1"));
            QFile::rename(g_log->fileName(), g_log->fileName() + QStringLiteral(".1"));
            g_log->open(QIODevice::WriteOnly | QIODevice::Append);
        }
        g_log->write(line.toUtf8());
        g_log->flush();
    }
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Exactly the application's names, so QStandardPaths yields the
    // application's own config and data directories.
    app.setOrganizationName(QStringLiteral("io.github.jarnose"));
    app.setApplicationName(QStringLiteral("onloc"));

    QString configPath;
    bool fake = false;
    double lat = 0, lon = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--config") && i + 1 < args.size()) {
            configPath = args.at(++i);
        } else if (args.at(i) == QLatin1String("--fake-position") && i + 1 < args.size()) {
            const QStringList p = args.at(++i).split(QLatin1Char(','));
            if (p.size() == 2) {
                fake = true;
                lat = p.at(0).toDouble();
                lon = p.at(1).toDouble();
            }
        } else {
            fprintf(stderr, "usage: %s [--config <file>] [--fake-position <lat>,<lon>]\n", qPrintable(args.value(0)));
            return 2;
        }
    }

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(logDir);
    QFile logFile(logDir + QStringLiteral("/daemon.log"));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append))
        g_log = &logFile;
    qInstallMessageHandler(logHandler);
    qInfo() << "onlocd starting";

    Daemon daemon(configPath);
    if (fake)
        daemon.setFakePosition(lat, lon);
    daemon.start();
    return app.exec();
}
