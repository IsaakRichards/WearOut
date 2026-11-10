import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.DBus 2.0

Page {
    id: page
    property string linkState: "Disconnected"
    property string peer: ""

    DBusInterface {
        id: daemon
        service: "org.wearout.daemon"
        path: "/org/wearout/Daemon"
        iface: "org.wearout.Daemon"
        signalsEnabled: true

        function stateChanged(s) { page.linkState = s }
        function peerChanged(n) { page.peer = n }
        function log(line) { logModel.insert(0, { "line": line }); if (logModel.count > 300) logModel.remove(300) }
        function messageReceived(path, pkg, text) {
            logModel.insert(0, { "line": "⌚ " + path + ": " + text })
        }

        Component.onCompleted: {
            typedCall("state", [], function (s) { page.linkState = s })
            typedCall("watchAddress", [], function (a) { addressField.text = a })
            typedCall("peerName", [], function (n) { page.peer = n })
        }
    }

    ListModel { id: logModel }

    SilicaListView {
        anchors.fill: parent
        model: logModel

        PullDownMenu {
            MenuItem {
                text: qsTr("Disconnect")
                onClicked: daemon.call("disconnectWatch", [])
            }
            MenuItem {
                text: qsTr("Connect to emulator (dev)")
                onClicked: daemon.call("connectEmulator", ["127.0.0.1:5601"])
            }
            MenuItem {
                text: qsTr("Connect watch")
                enabled: addressField.acceptableInput
                onClicked: daemon.call("connectWatch", [addressField.text.toUpperCase()])
            }
        }

        header: Column {
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: "WearOut" }

            DetailItem { label: qsTr("Link"); value: page.linkState }
            DetailItem { label: qsTr("Watch"); value: page.peer.length ? page.peer : "—" }

            TextField {
                id: addressField
                width: parent.width
                label: qsTr("Watch Bluetooth address")
                placeholderText: "AA:BB:CC:DD:EE:FF"
                validator: RegExpValidator { regExp: /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/ }
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhPreferUppercase
                EnterKey.onClicked: focus = false
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Pair the watch in Settings › Bluetooth first. This is an experimental reverse-engineering build: expect raw protocol logs, not a finished companion.")
            }

            SectionHeader { text: qsTr("Protocol log") }
        }

        delegate: Label {
            x: Theme.horizontalPageMargin
            width: ListView.view.width - 2 * x
            text: model.line
            wrapMode: Text.WrapAnywhere
            font.family: "monospace"
            font.pixelSize: Theme.fontSizeTiny
        }

        VerticalScrollDecorator {}
    }
}
