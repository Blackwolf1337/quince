import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Play, Shuffle and Radio for a page header or the top search result.
RowLayout {
    id: buttons

    required property var target

    // the page plays its own track list when it has one
    signal playRequested()

    spacing: 10

    Button {
        text: qsTr("Play")
        icon.source: "icons/play.svg"
        highlighted: true
        visible: !!buttons.target.playPlaylistId || !!buttons.target.playVideoId || !!buttons.target.videoId
        onClicked: buttons.playRequested()
    }
    Button {
        text: qsTr("Shuffle")
        icon.source: "icons/shuffle.svg"
        visible: !!buttons.target.shufflePlaylistId
        onClicked: App.playback.playPlaylist(buttons.target.shufflePlaylistId, "", true)
    }
    Button {
        text: qsTr("Radio")
        icon.source: "icons/radio.svg"
        visible: !!buttons.target.radioPlaylistId
        onClicked: App.playback.playPlaylist(buttons.target.radioPlaylistId, "", false)
    }
}
