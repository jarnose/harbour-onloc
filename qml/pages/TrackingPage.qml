import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    readonly property var intervals: [60, 300, 900, 1800, 3600]
    readonly property bool hasDevice: AppController.thisDeviceId.length > 0

    Connections {
        target: AppController
        onErrorOccurred: if (status === PageStatus.Active) toast.show(message, true)
        onInfoOccurred: if (status === PageStatus.Active) toast.show(message, false)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("This phone")
            }

            SectionHeader { text: qsTr("Device") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: page.hasDevice
                      ? qsTr("Reporting as “%1”").arg(AppController.thisDeviceName || AppController.thisDeviceId)
                      : qsTr("This phone is not registered on the server yet.")
            }

            ComboBox {
                id: existing
                visible: AppController.deviceModel.count > 0
                width: parent.width
                label: qsTr("Use an existing device")
                description: qsTr("Pick the entry that represents this phone")
                menu: ContextMenu {
                    Repeater {
                        model: AppController.deviceModel
                        MenuItem {
                            // Shared devices belong to someone else; this
                            // phone can only report as one it owns.
                            visible: !model.shared
                            height: visible ? implicitHeight : 0
                            text: model.name
                            onClicked: AppController.useAsThisDevice(model.id)
                        }
                    }
                }
                currentIndex: -1
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("New device name")
                placeholderText: qsTr("New device name")
                text: "Sailfish phone"
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: AppController.registerThisDevice(text)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Create device")
                enabled: nameField.text.trim().length > 0 && !AppController.busy
                onClicked: AppController.registerThisDevice(nameField.text)
            }

            SectionHeader { text: qsTr("Location reporting") }

            TextSwitch {
                id: trackSwitch
                width: parent.width
                enabled: page.hasDevice
                text: qsTr("Send this phone's position")
                description: qsTr("Works while the app is running, also in the background. "
                                  + "Close the app and reporting stops.")
                checked: AppController.trackingEnabled
                automaticCheck: false
                onClicked: AppController.trackingEnabled = !AppController.trackingEnabled
            }

            ComboBox {
                id: intervalBox
                width: parent.width
                enabled: page.hasDevice
                label: qsTr("Interval")
                menu: ContextMenu {
                    Repeater {
                        model: page.intervals
                        MenuItem {
                            text: modelData < 3600 ? qsTr("%1 min").arg(modelData / 60) : qsTr("%1 h").arg(modelData / 3600)
                            onClicked: AppController.trackingInterval = modelData
                        }
                    }
                }
                currentIndex: Math.max(0, page.intervals.indexOf(AppController.trackingInterval))
            }

            DetailItem {
                visible: AppController.trackingStatus.length > 0
                label: qsTr("Status")
                value: AppController.trackingStatus
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Send position now")
                enabled: page.hasDevice && !AppController.busy
                onClicked: AppController.sendLocationNow()
            }

            SectionHeader { text: qsTr("Background service") }

            Label {
                visible: !AppController.daemonInstalled
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: AppController.daemonInstallable
                      ? qsTr("Install the background service from Settings to keep reporting after the app "
                             + "is closed, and to let the OnLoc server ring, lock or flash this phone.")
                      : qsTr("Install the onloc-daemon package to keep reporting after the app is closed, "
                             + "and to let the OnLoc server ring, lock or flash this phone.")
            }

            TextSwitch {
                visible: AppController.daemonInstalled
                width: parent.width
                enabled: page.hasDevice
                text: qsTr("Run in the background")
                description: qsTr("Hands this sign-in to the background service, which keeps reporting when the "
                                  + "app is closed and obeys ring, lock and flash. The sign-in is then also kept "
                                  + "in a file only you can read; switching this off or signing out deletes it.")
                checked: AppController.backgroundMode
                automaticCheck: false
                onClicked: AppController.backgroundMode = !AppController.backgroundMode
            }

            DetailItem {
                visible: AppController.daemonInstalled && AppController.backgroundMode
                label: qsTr("Service")
                value: AppController.daemonRunning ? qsTr("Running") : qsTr("Not running")
            }

            DetailItem {
                visible: AppController.daemonInstalled && AppController.backgroundMode && AppController.daemonRunning
                label: qsTr("Commands")
                value: AppController.daemonStatus.socket ? qsTr("Listening") : qsTr("Not connected")
            }

            DetailItem {
                visible: AppController.daemonInstalled && AppController.backgroundMode
                         && AppController.daemonRunning && !!AppController.daemonStatus.last_command
                label: qsTr("Last command")
                value: AppController.daemonStatus.last_command ? AppController.daemonStatus.last_command : ""
            }

            Label {
                visible: AppController.daemonInstalled && AppController.backgroundMode
                         && AppController.daemonStatus.signed_out === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("The server no longer accepts the background service's sign-in. Sign out and in again.")
            }
        }
    }

    Toast { id: toast }
}
