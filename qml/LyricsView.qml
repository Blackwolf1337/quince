import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Quince

// Lyrics of the playing track; synced lines follow the music, a click on one
// jumps there.
Item {
    id: view

    readonly property Lyrics lyrics: App.lyrics
    // size of synced lines; the Now Playing view shows them large
    property int lineSize: 20
    // size of lyrics without times
    property int plainSize: 15

    ListView {
        id: list

        anchors.fill: parent
        anchors.margins: 12
        clip: true
        model: view.lyrics.lines
        spacing: view.lyrics.synced ? 10 : 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        // keep the sung line in the upper middle while the user is not scrolling
        currentIndex: view.lyrics.currentLine
        highlightFollowsCurrentItem: true
        preferredHighlightBegin: height * 0.35
        preferredHighlightEnd: height * 0.35 + 40
        highlightRangeMode: view.lyrics.synced && !list.moving ? ListView.ApplyRange : ListView.NoHighlightRange
        highlightMoveDuration: Style.slow

        SmoothScroll {}

        delegate: Label {
            required property var modelData
            required property int index
            readonly property bool sung: view.lyrics.synced && index === view.lyrics.currentLine

            width: ListView.view.width
            text: modelData.text
            wrapMode: Text.Wrap
            color: sung ? Style.text : view.lyrics.synced && index < view.lyrics.currentLine ? Style.textDim
                                                                                                : Qt.lighter(Style.textDim, 1.15)
            font.pixelSize: view.lyrics.synced ? view.lineSize : view.plainSize
            font.weight: view.lyrics.synced ? Font.Bold : Font.Normal
            opacity: view.lyrics.synced && !sung ? 0.55 : 1
            // the sung line grows a little instead of turning bold, so nothing reflows
            scale: view.lyrics.synced && !sung ? 0.94 : 1
            transformOrigin: Item.Left

            Behavior on color {
                ColorAnimation { duration: Style.normal }
            }
            Behavior on opacity {
                NumberAnimation { duration: Style.normal }
            }
            Behavior on scale {
                NumberAnimation { duration: Style.slow; easing.type: Easing.OutCubic }
            }

            TapHandler {
                enabled: view.lyrics.synced && modelData.time >= 0
                onTapped: App.player.seek(modelData.time)
            }
            HoverHandler {
                enabled: view.lyrics.synced
                cursorShape: Qt.PointingHandCursor
            }
        }

        footer: Label {
            width: ListView.view.width
            topPadding: 16
            text: view.lyrics.source ? qsTr("Lyrics: %1").arg(view.lyrics.source) : ""
            color: Style.textDim
            font.pixelSize: 12
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: view.lyrics.loading
        visible: running
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 48
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: !view.lyrics.loading && view.lyrics.lines.length === 0
        text: App.playback.track.videoId ? qsTr("No lyrics found for this track.") : qsTr("Play something to see its lyrics.")
        color: Style.textDim
    }
}
