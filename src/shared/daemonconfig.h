#pragma once

#include <QString>

// What the application hands the background daemon (onlocd), as a 0600 JSON
// file in the application's own config directory.
//
// A file rather than anything cleverer because the two cannot share a secret
// any other way: the application's refresh token lives in the Sailfish
// Secrets vault, which is owner-only to the application, and the daemon is a
// different executable outside the sandbox. The file is written only when the
// user turns background reporting on, and removed when they turn it off or
// sign out. The token in it is a revocable refresh token, never the password.
struct DaemonConfig
{
    bool enabled = false;       // report this phone's position
    bool commands = true;       // listen for ring / lock / flash
    QString server;             // normalised base URL
    QString refreshToken;
    QString deviceId;           // the Onloc device this phone is
    int interval = 300;         // seconds between uploads

    bool usable() const { return !server.isEmpty() && !refreshToken.isEmpty() && !deviceId.isEmpty(); }

    // ~/.config/<org>/<app>/daemon.json - the same place for the application
    // (sandboxed, whitelisted) and the daemon, because both set the same
    // organisation and application names.
    static QString path();
    // ~/.local/share/<org>/<app>/ - the daemon's status and upload queue.
    static QString dataDir();
    static QString statusPath();
    static QString queuePath();
    // True where the daemon is installed: its binary is there, or (all that a
    // sandboxed process can see) it has written a status file.
    static bool daemonInstalled();

    static DaemonConfig load(const QString &file = QString());
    // Written atomically with owner-only permissions.
    bool save(const QString &file = QString()) const;
    static void remove(const QString &file = QString());
};
