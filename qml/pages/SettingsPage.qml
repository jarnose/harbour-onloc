import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
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
        }
    }
}
