import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.DBus 2.0

// Laid out for the Commodore Callback 8020: a 3.25" 480x640 portrait screen
// driven by a T9 keypad. Every action is reachable without touch:
//   1 = connect selected watch   2 = disconnect   3 = rescan paired watches
//   0 = connect emulator (dev)   up/down + OK = pick a watch
// Touch (and the pull-down menu) still work on devices that have it.
Page {
    id: page
    property string linkState: "Disconnected"
    property string peer: ""
    property string profile: ""
    property string selectedAddress: ""

    function rescan() {
        daemon.typedCall("findWatches", [], function (list) {
            watchModel.clear()
            for (var i = 0; i < list.length; ++i) {
                var p = list[i].split("|")
                watchModel.append({ "address": p[0], "name": p[1], "profile": p[2] })
            }
            if (watchModel.count > 0 && selectedAddress === "")
                selectedAddress = watchModel.get(0).address
        })
    }
    function connectSelected() {
        if (selectedAddress !== "")
            daemon.call("connectWatch", [selectedAddress])
    }
    function addLog(line) {
        logModel.insert(0, { "line": line })
        if (logModel.count > 200)
            logModel.remove(200)
    }

    DBusInterface {
        id: daemon
        service: "org.wearout.daemon"
        path: "/org/wearout/Daemon"
        iface: "org.wearout.Daemon"
        signalsEnabled: true

        function stateChanged(s) {
            page.linkState = s
            typedCall("activeProfile", [], function (p) { page.profile = p })
        }
        function peerChanged(n) { page.peer = n }
        function log(line) { page.addLog(line) }
        function messageReceived(path, pkg, text) { page.addLog("⌚ " + path + ": " + text) }

        Component.onCompleted: {
            typedCall("state", [], function (s) { page.linkState = s })
            typedCall("watchAddress", [], function (a) { if (a) page.selectedAddress = a })
            typedCall("peerName", [], function (n) { page.peer = n })
            typedCall("activeProfile", [], function (p) { page.profile = p })
            page.rescan()
        }
    }

    ListModel { id: watchModel }
    ListModel { id: logModel }

    // Keypad shortcuts. Page-level so they work whatever has focus.
    Keys.onPressed: {
        switch (event.key) {
        case Qt.Key_1: connectSelected(); event.accepted = true; break
        case Qt.Key_2: daemon.call("disconnectWatch", []); event.accepted = true; break
        case Qt.Key_3: rescan(); event.accepted = true; break
        case Qt.Key_0: daemon.call("connectEmulator", ["127.0.0.1:5601"]); event.accepted = true; break
        }
    }
    Component.onCompleted: forceActiveFocus()

    SilicaListView {
        id: logView
        anchors.fill: parent
        model: logModel

        PullDownMenu {
            MenuItem { text: qsTr("Connect emulator (dev)"); onClicked: daemon.call("connectEmulator", ["127.0.0.1:5601"]) }
            MenuItem { text: qsTr("Rescan paired watches"); onClicked: page.rescan() }
        }

        header: Column {
            width: logView.width
            spacing: Theme.paddingSmall

            PageHeader { title: "WearOut"; description: page.linkState }

            DetailItem { label: qsTr("Watch"); value: page.peer.length ? page.peer : "—" }
            DetailItem { label: qsTr("Profile"); value: page.profile.length ? page.profile : "—" }

            SectionHeader { text: qsTr("Paired watches") }

            Label {
                visible: watchModel.count === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("No known watch is paired. Pair it in Settings › Bluetooth, then press 3.")
            }

            Repeater {
                model: watchModel
                delegate: BackgroundItem {
                    width: parent.width
                    height: Theme.itemSizeSmall
                    highlighted: down || model.address === page.selectedAddress
                    onClicked: page.selectedAddress = model.address
                    Label {
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        truncationMode: TruncationMode.Fade
                        text: model.name
                        color: highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }

            Row {
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Button {
                    text: qsTr("1 Connect")
                    enabled: page.selectedAddress !== ""
                    onClicked: page.connectSelected()
                }
                Button {
                    text: qsTr("2 Stop")
                    onClicked: daemon.call("disconnectWatch", [])
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: qsTr("Keys: 1 connect · 2 stop · 3 rescan · 0 emulator")
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
