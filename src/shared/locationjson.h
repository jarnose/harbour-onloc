#pragma once

#include <QGeoPositionInfo>
#include <QJsonObject>
#include <QString>

// The body of one location upload, as the Onloc API wants it, from a fix.
// Shared by the application's own tracker and the daemon. Battery is read
// from sysfs and left out when unreadable.
QJsonObject locationFromFix(const QString &deviceId, const QGeoPositionInfo &info);

// Battery percentage, or -1; *charging set when a battery was read.
int readBattery(bool *charging);
