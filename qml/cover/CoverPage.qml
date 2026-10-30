import QtQuick 2.0
import Sailfish.Silica 1.0
import "../pages/Format.js" as Format

CoverBackground {
    id: cover

    property var _first: ({})
    property bool hasDevices: false

    // MapListModel.get() is a plain call, not a bound property, so poll it.
    function refresh() {
        var m = AppController.deviceModel
        var has = AppController.loggedIn && m.count > 0
        // This phone's entry if it is registered, else the first device.
        var row = AppController.thisDeviceId ? m.indexOfId(AppController.thisDeviceId) : -1
        // _first before hasDevices: the labels bind to both, and must never
        // see hasDevices true while _first is still empty.
        _first = has ? m.get(row >= 0 ? row : 0) : ({})
        hasDevices = has
    }

    Timer {
        interval: 5000
        running: cover.status === Cover.Active
        repeat: true
        triggeredOnStart: true
        onTriggered: cover.refresh()
    }

    Label {
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            left: parent.left
            right: parent.right
            leftMargin: Theme.paddingMedium
            rightMargin: Theme.paddingMedium
        }
        text: "OnLoc"
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
        horizontalAlignment: Text.AlignHCenter
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingMedium
        spacing: Theme.paddingSmall
        visible: hasDevices

        Label {
            width: parent.width
            text: hasDevices ? (_first.name || "") : ""
            horizontalAlignment: Text.AlignHCenter
            truncationMode: TruncationMode.Fade
        }

        Label {
            width: parent.width
            text: hasDevices && _first.hasLocation ? Format.ago(_first.updatedAt) : qsTr("No location yet")
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            width: parent.width
            visible: AppController.trackingEnabled
            text: qsTr("Reporting")
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !hasDevices
        text: AppController.loggedIn ? qsTr("No devices") : qsTr("Not signed in")
        color: Theme.secondaryColor
    }

    CoverActionList {
        enabled: AppController.loggedIn
        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: AppController.refreshDevices()
        }
    }
}
