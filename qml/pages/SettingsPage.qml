import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    Connections {
        target: AppController
        onErrorOccurred: if (page.status === PageStatus.Active) toast.show(message, true)
        onInfoOccurred: if (page.status === PageStatus.Active) toast.show(message, false)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader { text: qsTr("Map") }

            TextSwitch {
                text: qsTr("Show a base map")
                description: qsTr("The map tiles come from somebody else's servers, which learn the area "
                                  + "of every device you open. The positions themselves are never sent there.")
                checked: AppController.baseMapEnabled
                automaticCheck: false
                onClicked: AppController.baseMapEnabled = !AppController.baseMapEnabled
            }

            // Only shown when the map is on: a URL field above a switch that
            // ignores it would invite filling in something that does nothing.
            Column {
                width: parent.width
                visible: AppController.baseMapEnabled

                TextField {
                    width: parent.width
                    label: qsTr("Tile address")
                    text: AppController.mapTileUrl
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: {
                        AppController.mapTileUrl = text
                        focus = false
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            AppController.mapTileUrl = text
                    }
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    text: qsTr("Either a TileJSON address or a {z}/{x}/{y} template. The default is "
                               + "OpenFreeMap, which needs no account and allows this use; its attribution "
                               + "is drawn on the map. Vector tiles only for now - an address serving "
                               + "images will say so.")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }

                Button {
                    x: Theme.horizontalPageMargin
                    visible: AppController.mapTileUrl !== AppController.defaultMapTileUrl
                    text: qsTr("Back to the default source")
                    onClicked: AppController.mapTileUrl = AppController.defaultMapTileUrl
                }
            }

            // ---- background service ----
            //
            // The section is absent when the build carries no service to
            // install and none is installed, rather than present and inert.
            // See AppController::installDaemon() and docs/daemon.md.
            SectionHeader {
                text: qsTr("Background service")
                visible: AppController.daemonInstallable || AppController.daemonInstalled
            }

            Label {
                visible: AppController.daemonInstallable || AppController.daemonInstalled
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: AppController.daemonInstalled
                      ? (AppController.daemonOutdated
                         ? qsTr("The background service is installed, but it is version %1 and this app is %2. "
                                + "Update it to match.")
                               .arg(AppController.daemonStatus.version).arg(AppController.appVersion)
                         : qsTr("The background service is installed. Switch it on under This phone."))
                      : qsTr("Reporting this phone's position after the app is closed, and ringing, locking "
                             + "or flashing it from the server, needs a small second package. It cannot be "
                             + "part of this app: it has to keep running and drive sound and the display, "
                             + "which the sandbox does not allow. This app carries it and the system "
                             + "installs it - you will be asked to confirm.")
            }

            Button {
                visible: AppController.daemonInstallable
                x: Theme.horizontalPageMargin
                text: !AppController.daemonInstalled
                      ? qsTr("Install the service")
                      : (AppController.daemonOutdated ? qsTr("Update the service") : qsTr("Reinstall the service"))
                onClicked: AppController.installDaemon()
            }

            Label {
                // Said before the attempt, not only after a refusal: an
                // unsigned package is rejected without explaining itself.
                visible: AppController.daemonInstallable && !AppController.daemonInstalled
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: qsTr("If the install is refused, allow untrusted software in "
                           + "Settings | Untrusted software, then try again.")
            }
        }
    }

    Toast { id: toast }
}
