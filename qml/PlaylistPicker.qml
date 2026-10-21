import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// "Add to playlist": the account's own playlists, or a new one.
Dialog {
    id: picker

    required property var item

    title: qsTr("Add to playlist")
    modal: true

    background: Rectangle {
        color: Style.surfaceHigh
        radius: 12
        border.color: "#22ffffff"
    }
    Overlay.modal: Rectangle {
        color: "#99000000"
    }
    anchors.centerIn: Overlay.overlay
    width: 420
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 80 : 560)
    standardButtons: Dialog.Cancel
    onClosed: destroy()
    Component.onCompleted: App.account.reloadPlaylists()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: newTitle
                Layout.fillWidth: true
                placeholderText: qsTr("New playlist")
                onAccepted: create.clicked()
            }
            Button {
                id: create
                text: qsTr("Create")
                enabled: newTitle.text.trim().length > 0
                onClicked: {
                    App.account.createPlaylist(newTitle.text.trim(), picker.item.videoId)
                    picker.close()
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.account.playlists.filter(p => !!p.owned)
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width
                text: modelData.title
                icon.source: "icons/playlist.svg"
                onClicked: {
                    App.account.addToPlaylist(modelData.browseId, picker.item.videoId, modelData.title)
                    picker.close()
                }
            }
        }
    }
}
