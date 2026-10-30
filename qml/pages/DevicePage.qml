import QtQuick 2.0
import Sailfish.Silica 1.0
import io.github.jarnose.onloc 1.0
import "Format.js" as Format

Page {
    id: page

    property string deviceId
    // Re-read from the model on every refresh so the page follows the list.
    property var device: ({})
    property int historyHours: 24

    function reload() {
        var row = AppController.deviceModel.indexOfId(deviceId)
        device = row >= 0 ? AppController.deviceModel.get(row) : ({})
    }

    Component.onCompleted: {
        reload()
        AppController.loadHistory(deviceId, historyHours)
    }
    Component.onDestruction: AppController.clearHistory()

    Connections {
        target: AppController.deviceModel
        onModelReset: page.reload()
    }

    Connections {
        target: AppController
        onErrorOccurred: if (status === PageStatus.Active) toast.show(message, true)
        onInfoOccurred: if (status === PageStatus.Active) toast.show(message, false)
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: AppController.historyModel

        PullDownMenu {
            busy: AppController.busy
            MenuItem {
                text: qsTr("Refresh")
                onClicked: {
                    AppController.refreshDevices()
                    AppController.loadHistory(page.deviceId, page.historyHours)
                }
            }
            MenuItem {
                text: qsTr("Flash")
                visible: !!device.canFlash
                onClicked: AppController.flashDevice(page.deviceId)
            }
            MenuItem {
                text: qsTr("Lock")
                visible: !!device.canLock
                onClicked: pageStack.push(lockPage)
            }
            MenuItem {
                text: qsTr("Ring")
                visible: !!device.canRing
                onClicked: AppController.ringDevice(page.deviceId)
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: device.name || ""
                description: device.shared ? qsTr("Shared with you") : ""
            }

            DetailItem {
                label: qsTr("Status")
                value: device.isConnected ? qsTr("Connected") : qsTr("Not connected")
            }

            DetailItem {
                label: qsTr("Last seen")
                value: device.hasLocation ? Format.ago(device.updatedAt) + " (" + Format.dateTime(device.updatedAt) + ")" : qsTr("No location yet")
            }

            DetailItem {
                visible: device.hasLocation === true
                label: qsTr("Position")
                value: device.hasLocation ? Format.coords(device.latitude, device.longitude) : ""
            }

            DetailItem {
                visible: device.hasLocation === true && device.accuracy !== undefined
                label: qsTr("Accuracy")
                value: device.accuracy !== undefined ? qsTr("%1 m").arg(Math.round(device.accuracy)) : ""
            }

            DetailItem {
                visible: device.battery !== undefined
                label: qsTr("Battery")
                value: device.battery !== undefined
                       ? device.battery + " %" + (device.charging ? " " + qsTr("(charging)") : "")
                       : ""
            }

            // The base map: the last 24 h as a route with the newest point
            // marked, or just that marker when there is no history. Off
            // switches it (and every tile request) off entirely.
            Item {
                id: mapBox
                visible: AppController.baseMapEnabled && mapPoints.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: visible ? width * 0.75 : 0

                property var mapPoints: AppController.historyTrack.length > 0
                                        ? AppController.historyTrack
                                        : (device.hasLocation === true
                                           ? [{ latitude: device.latitude, longitude: device.longitude }]
                                           : [])

                MapCanvas {
                    id: baseMap
                    anchors.fill: parent
                    track: mapBox.visible ? mapBox.mapPoints : []
                    tileUrl: mapBox.visible ? AppController.mapTileUrl : ""
                    darkMode: Theme.colorScheme === Theme.LightOnDark
                    routeColor: Theme.highlightColor

                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: baseMap.loading
                    }
                }
            }

            // Why the map is blank, when it is: a source that cannot be used
            // is worth saying out loud, or the switch looks broken.
            Label {
                visible: mapBox.visible && baseMap.error.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: baseMap.error
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: device.hasLocation === true
                text: qsTr("Open in Maps")
                onClicked: Qt.openUrlExternally(Format.geoUrl(device.latitude, device.longitude))
            }

            Item { width: 1; height: Theme.paddingLarge }

            SectionHeader {
                text: qsTr("Last %1 h").arg(page.historyHours)
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Small
                running: AppController.historyBusy
                height: running ? implicitHeight : 0
            }
        }

        delegate: ListItem {
            id: row
            contentHeight: Theme.itemSizeMedium
            onClicked: Qt.openUrlExternally(Format.geoUrl(model.latitude, model.longitude))

            Column {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: Format.dateTime(model.createdAt)
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }

                Label {
                    width: parent.width
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                    text: Format.coords(model.latitude, model.longitude)
                          + (model.accuracy !== undefined ? "  ±" + Math.round(model.accuracy) + " m" : "")
                          + (model.battery !== undefined ? "  ·  " + model.battery + " %" : "")
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0 && !AppController.historyBusy
            text: qsTr("No history")
            hintText: qsTr("No positions reported in this period")
        }

        VerticalScrollDecorator {}
    }

    Toast { id: toast }

    Component {
        id: lockPage
        Dialog {
            canAccept: true
            onAccepted: AppController.lockDevice(page.deviceId, msgField.text)

            Column {
                width: parent.width

                DialogHeader {
                    acceptText: qsTr("Lock")
                    title: qsTr("Lock %1").arg(device.name || "")
                }

                TextField {
                    id: msgField
                    width: parent.width
                    label: qsTr("Message on the lock screen (optional)")
                    placeholderText: qsTr("Message")
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: accept()
                }
            }
        }
    }
}
