import QtQuick 2.0
import Sailfish.Silica 1.0
import "Format.js" as Format

Page {
    id: page

    property bool loginShown: false

    // Open the login page once, when it turns out there is no session to
    // restore. Re-opened after a logout or an expired session.
    function maybeShowLogin() {
        if (!AppController.restoring && !AppController.loggedIn && !loginShown) {
            loginShown = true
            pageStack.push(Qt.resolvedUrl("LoginPage.qml"))
        }
    }

    Component.onCompleted: maybeShowLogin()

    Connections {
        target: AppController
        onRestoringChanged: page.maybeShowLogin()
        onLoggedInChanged: {
            if (AppController.loggedIn)
                page.loginShown = false
            else
                page.maybeShowLogin()
        }
        onErrorOccurred: if (status === PageStatus.Active) toast.show(message, true)
        onInfoOccurred: if (status === PageStatus.Active) toast.show(message, false)
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: AppController.deviceModel

        header: PageHeader {
            title: qsTr("Devices")
            description: AppController.loggedIn ? AppController.username + " @ " + AppController.serverUrl : ""
        }

        PullDownMenu {
            busy: AppController.busy
            MenuItem {
                text: qsTr("Sign out")
                visible: AppController.loggedIn
                onClicked: remorse.execute(qsTr("Signing out"), function() { AppController.logout() })
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("This phone")
                visible: AppController.loggedIn
                onClicked: pageStack.push(Qt.resolvedUrl("TrackingPage.qml"))
            }
            MenuItem {
                text: qsTr("Refresh")
                visible: AppController.loggedIn
                onClicked: AppController.refreshDevices()
            }
            MenuItem {
                text: qsTr("Sign in")
                visible: !AppController.loggedIn
                onClicked: pageStack.push(Qt.resolvedUrl("LoginPage.qml"))
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeLarge
            onClicked: pageStack.push(Qt.resolvedUrl("DevicePage.qml"), { deviceId: model.id })

            Rectangle {
                id: dot
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.paddingMedium
                height: width
                radius: width / 2
                color: model.isConnected ? "#4caf50" : Theme.secondaryColor
                opacity: model.isConnected ? 1.0 : Theme.opacityLow
            }

            Column {
                anchors {
                    left: dot.right
                    leftMargin: Theme.paddingMedium
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.name + (model.shared ? "  ·  " + qsTr("shared") : "")
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }

                Label {
                    width: parent.width
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                    text: !model.hasLocation
                          ? qsTr("No location yet")
                          : Format.ago(model.updatedAt)
                            + (model.battery !== undefined ? "  ·  " + model.battery + " %" : "")
                }
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0 && !AppController.busy && !AppController.restoring
            text: AppController.loggedIn ? qsTr("No devices") : qsTr("Not signed in")
            hintText: AppController.loggedIn
                      ? qsTr("Add this phone from the pull-down menu")
                      : qsTr("Pull down to sign in")
        }

        VerticalScrollDecorator {}
    }

    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Large
        running: AppController.restoring || (AppController.busy && listView.count === 0)
    }

    RemorsePopup { id: remorse }

    Toast { id: toast }
}
