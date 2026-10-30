#include "locationjson.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGeoCoordinate>

#include <cmath>

int readBattery(bool *charging)
{
    *charging = false;
    const QDir dir(QStringLiteral("/sys/class/power_supply"));
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        QFile type(dir.filePath(name + QStringLiteral("/type")));
        if (!type.open(QIODevice::ReadOnly) || type.readAll().trimmed() != "Battery")
            continue;
        QFile cap(dir.filePath(name + QStringLiteral("/capacity")));
        if (!cap.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const int level = cap.readAll().trimmed().toInt(&ok);
        if (!ok)
            continue;
        QFile st(dir.filePath(name + QStringLiteral("/status")));
        if (st.open(QIODevice::ReadOnly)) {
            const QByteArray s = st.readAll().trimmed();
            *charging = s == "Charging" || s == "Full";
        }
        return level;
    }
    return -1;
}

QJsonObject locationFromFix(const QString &deviceId, const QGeoPositionInfo &info)
{
    const QGeoCoordinate c = info.coordinate();
    QJsonObject loc;
    // Onloc stores device ids as integers; the API accepts a number.
    loc.insert(QStringLiteral("device_id"), deviceId.toLongLong());
    loc.insert(QStringLiteral("latitude"), c.latitude());
    loc.insert(QStringLiteral("longitude"), c.longitude());
    if (info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
        loc.insert(QStringLiteral("accuracy"), info.attribute(QGeoPositionInfo::HorizontalAccuracy));
    if (!std::isnan(c.altitude()))
        loc.insert(QStringLiteral("altitude"), c.altitude());
    if (info.hasAttribute(QGeoPositionInfo::VerticalAccuracy))
        loc.insert(QStringLiteral("altitude_accuracy"), info.attribute(QGeoPositionInfo::VerticalAccuracy));
    if (info.hasAttribute(QGeoPositionInfo::GroundSpeed))
        loc.insert(QStringLiteral("speed"), info.attribute(QGeoPositionInfo::GroundSpeed));
    if (info.hasAttribute(QGeoPositionInfo::Direction))
        loc.insert(QStringLiteral("bearing"), info.attribute(QGeoPositionInfo::Direction));
    loc.insert(QStringLiteral("provider"), QStringLiteral("sailfish"));
    bool charging = false;
    const int battery = readBattery(&charging);
    if (battery >= 0) {
        loc.insert(QStringLiteral("battery"), battery);
        loc.insert(QStringLiteral("charging"), charging);
    }
    loc.insert(QStringLiteral("created_at"),
               (info.timestamp().isValid() ? info.timestamp() : QDateTime::currentDateTimeUtc())
                   .toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ")));
    return loc;
}
