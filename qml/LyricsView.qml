import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
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

    // 83.4 -> "1:23"
    function timeCode(seconds: real): string {
        const s = Math.floor(seconds)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    ListView {
        id: list

        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        clip: true
        model: view.lyrics.lines
        spacing: view.lyrics.synced ? (App.settings.lyricsEffect === "fancy" ? 14 : 10) : 2
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

        // Pear's line effects: fancy (large, the sung line glows and wobbles),
        // scale (the sung line grows), offset (it moves right), focus (only it
        // is bright); the romanized line sits under each line
        delegate: Item {
            id: line

            required property var modelData
            required property int index
            readonly property bool synced: view.lyrics.synced
            readonly property bool sung: synced && index === view.lyrics.currentLine
            readonly property string effect: App.settings.lyricsEffect
            readonly property bool fancy: synced && effect === "fancy"
            readonly property bool timed: synced && App.settings.lyricsTimeCodes && modelData.time >= 0

            // the side room lies inside the list, so the glow is not cut off at its edge
            x: 12
            width: ListView.view.width - 24
            implicitHeight: body.implicitHeight

            onSungChanged: if (sung && fancy) wobble.restart()

            Label {
                id: timeCode
                visible: line.timed
                // on the baseline of the line's first row
                y: body.y + lineText.baselineOffset - baselineOffset
                text: view.timeCode(line.modelData.time)
                color: Style.textDim
                font.pixelSize: 12
                font.features: { "tnum": 1 }
            }

            Column {
                id: body

                readonly property real indent: timeCode.visible ? timeCode.implicitWidth + 10 : 0
                x: indent + (line.sung && line.effect === "offset" ? (line.width - indent) * 0.05 : 0)
                // a grown line must still fit
                width: (line.width - indent) * (line.synced && line.effect === "scale" ? 0.83 : 1)
                spacing: 2
                opacity: !line.synced || line.sung ? 1 : 0.33
                scale: !line.synced ? 1 : line.sung ? (line.effect === "scale" ? 1.2 : 1) : (line.fancy ? 0.95 : 1)
                transformOrigin: Item.Left
                transform: Translate {
                    id: shift
                }

                Behavior on x {
                    NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
                }
                Behavior on opacity {
                    NumberAnimation { duration: 330 }
                }
                Behavior on scale {
                    NumberAnimation { duration: line.fancy ? Style.slow : 166; easing.type: Easing.OutCubic }
                }

                Label {
                    id: lineText
                    width: parent.width
                    // the pauses of synced lyrics show a mark, so the line still moves on
                    text: line.modelData.text || (line.synced ? App.settings.lyricsGapText : "")
                    wrapMode: Text.Wrap
                    color: line.synced ? Style.text : Qt.lighter(Style.textDim, 1.15)
                    font.pixelSize: !line.synced ? view.plainSize : line.fancy ? Math.round(view.lineSize * 1.25) : view.lineSize
                    font.weight: line.synced && (line.fancy || line.sung) ? Font.Bold : Font.Normal

                    // fancy: a glow that fades out after the line starts
                    MultiEffect {
                        id: glow
                        anchors.fill: parent
                        z: -1
                        source: lineText
                        visible: Style.effects && line.fancy && line.sung && shadowOpacity > 0
                        shadowEnabled: true
                        shadowColor: "#70ffffff"
                        shadowBlur: 0.8
                        shadowOpacity: 0
                        shadowHorizontalOffset: 0
                        shadowVerticalOffset: 0
                    }
                }
                Label {
                    width: parent.width
                    visible: text !== ""
                    text: line.modelData.romanized || ""
                    wrapMode: Text.Wrap
                    color: Style.textDim
                    font.pixelSize: Math.round(lineText.font.pixelSize * 0.6)
                }
            }

            // the glow fades over 2 s, the line wobbles for 1 s (Pear's timing)
            ParallelAnimation {
                id: wobble
                NumberAnimation { target: glow; property: "shadowOpacity"; from: 1; to: 0; duration: 2000 }
                SequentialAnimation {
                    NumberAnimation { target: shift; property: "y"; to: 1.75; duration: 333 }
                    NumberAnimation { target: shift; property: "y"; to: -1.75; duration: 333 }
                    NumberAnimation { target: shift; property: "y"; to: 0; duration: 334 }
                }
            }

            TapHandler {
                enabled: line.synced && line.modelData.time >= 0
                onTapped: App.player.seek(line.modelData.time)
            }
            HoverHandler {
                enabled: line.synced
                cursorShape: Qt.PointingHandCursor
            }
        }

        footer: Label {
            x: 12
            width: ListView.view.width - 24
            topPadding: 16
            text: !view.lyrics.source ? ""
                : view.lyrics.sourceUrl ? qsTr("Lyrics: %1").arg("<a href=\"%1\">%2</a>".arg(view.lyrics.sourceUrl).arg(view.lyrics.source))
                : qsTr("Lyrics: %1").arg(view.lyrics.source)
            textFormat: view.lyrics.sourceUrl ? Text.StyledText : Text.PlainText
            linkColor: Style.textDim
            color: Style.textDim
            font.pixelSize: 12
            onLinkActivated: link => Qt.openUrlExternally(link)

            HoverHandler {
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
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
