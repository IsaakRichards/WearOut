import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    Column {
        anchors.centerIn: parent
        spacing: Theme.paddingSmall
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "WearOut"
            color: Theme.highlightColor
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "⌚"
            font.pixelSize: Theme.fontSizeHuge
        }
    }
}
