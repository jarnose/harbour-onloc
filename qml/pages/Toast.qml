import QtQuick 2.0
import Sailfish.Silica 1.0

// A short-lived message at the bottom of the page. Call show().
Rectangle {
    id: toast
    property bool isError: false

    function show(message, error) {
        label.text = message
        isError = error
        anim.restart()
    }

    anchors {
        bottom: parent.bottom
        bottomMargin: Theme.paddingLarge
        horizontalCenter: parent.horizontalCenter
    }
    width: Math.min(parent.width - 2 * Theme.horizontalPageMargin,
                    label.implicitWidth + 2 * Theme.paddingLarge)
    height: label.implicitHeight + 2 * Theme.paddingMedium
    radius: Theme.paddingSmall
    color: isError ? Theme.rgba(Theme.errorColor, 0.9) : Theme.rgba(Theme.highlightBackgroundColor, 0.95)
    opacity: 0
    visible: opacity > 0
    z: 10

    Label {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.primaryColor
    }

    SequentialAnimation {
        id: anim
        NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 150 }
        PauseAnimation { duration: 3500 }
        NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 400 }
    }
}
