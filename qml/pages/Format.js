.pragma library

// "3 min ago" style text for an ISO timestamp from the server.
function ago(iso) {
    if (!iso)
        return qsTr("never")
    var then = new Date(iso)
    if (isNaN(then.getTime()))
        return ""
    var s = Math.max(0, Math.round((Date.now() - then.getTime()) / 1000))
    if (s < 60)
        return qsTr("just now")
    var m = Math.round(s / 60)
    if (m < 60)
        return qsTr("%1 min ago").arg(m)
    var h = Math.round(m / 60)
    if (h < 48)
        return qsTr("%1 h ago").arg(h)
    return qsTr("%1 days ago").arg(Math.round(h / 24))
}

function dateTime(iso) {
    var d = new Date(iso)
    return isNaN(d.getTime()) ? "" : Qt.formatDateTime(d, Qt.DefaultLocaleShortDate)
}

function coords(lat, lon) {
    return lat.toFixed(5) + ", " + lon.toFixed(5)
}

function geoUrl(lat, lon) {
    return "geo:" + lat.toFixed(6) + "," + lon.toFixed(6)
}
