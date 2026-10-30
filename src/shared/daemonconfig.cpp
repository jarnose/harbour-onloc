#include "daemonconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

QString DaemonConfig::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/daemon.json");
}

QString DaemonConfig::dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString DaemonConfig::statusPath()
{
    return dataDir() + QStringLiteral("/daemon-status.json");
}

QString DaemonConfig::queuePath()
{
    return dataDir() + QStringLiteral("/upload-queue.jsonl");
}

bool DaemonConfig::daemonInstalled()
{
    // The binary is not visible from inside the Sailjail sandbox (which
    // shows /usr/bin only in part), so the application cannot rely on seeing
    // it. The daemon writes its status file as soon as it starts, configured
    // or not, and that directory is the application's own - so the file's
    // existence is the signal that works from both sides.
    return QFileInfo::exists(QStringLiteral("/usr/bin/onlocd"))
            || QFileInfo::exists(statusPath());
}

DaemonConfig DaemonConfig::load(const QString &file)
{
    DaemonConfig c;
    QFile f(file.isEmpty() ? path() : file);
    if (!f.open(QIODevice::ReadOnly))
        return c;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    c.enabled = o.value(QStringLiteral("enabled")).toBool(false);
    c.commands = o.value(QStringLiteral("commands")).toBool(true);
    c.server = o.value(QStringLiteral("server")).toString();
    c.refreshToken = o.value(QStringLiteral("refresh_token")).toString();
    c.deviceId = o.value(QStringLiteral("device_id")).toString();
    c.interval = qBound(30, o.value(QStringLiteral("interval")).toInt(300), 86400);
    return c;
}

bool DaemonConfig::save(const QString &file) const
{
    const QString target = file.isEmpty() ? path() : file;
    QDir().mkpath(QFileInfo(target).absolutePath());

    QJsonObject o;
    o.insert(QStringLiteral("enabled"), enabled);
    o.insert(QStringLiteral("commands"), commands);
    o.insert(QStringLiteral("server"), server);
    o.insert(QStringLiteral("refresh_token"), refreshToken);
    o.insert(QStringLiteral("device_id"), deviceId);
    o.insert(QStringLiteral("interval"), interval);

    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Before any byte goes in: the file holds a token.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    const bool ok = out.commit();
    // QSaveFile renames its temporary file over the target; make sure the
    // result carries the restrictive mode whatever the platform did.
    if (ok)
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return ok;
}

void DaemonConfig::remove(const QString &file)
{
    QFile::remove(file.isEmpty() ? path() : file);
}
