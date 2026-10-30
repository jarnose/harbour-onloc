import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    readonly property bool ready: serverField.text.length > 0 && userField.text.length > 0
                                  && passField.text.length > 0 && !AppController.busy

    function submit() {
        errorLabel.text = ""
        AppController.login(serverField.text, userField.text, passField.text)
    }

    Connections {
        target: AppController
        onLoginSucceeded: pageStack.pop()
        onErrorOccurred: errorLabel.text = message
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Sign in")
            }

            TextField {
                id: serverField
                width: parent.width
                label: qsTr("Server address")
                placeholderText: "https://onloc.example.com"
                text: AppController.serverUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: userField.focus = true
            }

            TextField {
                id: userField
                width: parent.width
                label: qsTr("Username")
                placeholderText: label
                text: AppController.username
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passField.focus = true
            }

            PasswordField {
                id: passField
                width: parent.width
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: if (page.ready) page.submit()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sign in")
                enabled: page.ready
                onClicked: page.submit()
            }

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                id: errorLabel
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                visible: text.length > 0
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: qsTr("Your own Onloc server. Plain http:// is fine on a home network; "
                           + "anything reachable from the internet should use https://.")
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: AppController.busy
        }
    }
}
