import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Beside the pages (and in the Now Playing view): the play queue and the lyrics.
Item {
    id: panel

    readonly property Queue queue: App.playback.queue
    property alias tab: tabs.currentIndex
    property alias lyricsSize: lyricsView.lineSize
    property alias plainLyricsSize: lyricsView.plainSize

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Item {}

            TabButton {
                text: qsTr("Up next")
            }
            TabButton {
                text: qsTr("Lyrics")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 8
                    Layout.topMargin: 4

                    Label {
                        Layout.fillWidth: true
                        text: panel.queue.count > 0 ? qsTr("%n track(s)", "", panel.queue.count) : ""
                        color: Style.textDim
                        font.pixelSize: 13
                    }
                    ToolButton {
                        icon.source: "icons/shuffle.svg"
                        enabled: panel.queue.count > panel.queue.current + 2
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Shuffle the rest")
                        onClicked: panel.queue.shuffleRest()
                    }
                    ToolButton {
                        text: qsTr("Clear")
                        enabled: panel.queue.count > 1
                        onClicked: panel.queue.clearRest()
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: panel.queue
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    SmoothScroll {}

                    add: Transition {
                        ParallelAnimation {
                            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal }
                            NumberAnimation { property: "x"; from: 40; to: 6; duration: Style.normal; easing.type: Easing.OutCubic }
                        }
                    }
                    remove: Transition {
                        ParallelAnimation {
                            NumberAnimation { property: "opacity"; to: 0; duration: Style.fast }
                            NumberAnimation { property: "x"; to: -40; duration: Style.normal; easing.type: Easing.InCubic }
                        }
                    }
                    displaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: Style.normal; easing.type: Easing.OutCubic }
                    }
                    move: Transition {
                        NumberAnimation { properties: "x,y"; duration: Style.normal; easing.type: Easing.OutCubic }
                    }

                    // ItemRow's required "item" is filled from the model role of that name
                    delegate: ItemRow {
                        required property int index
                        required property bool isCurrent
                        x: 6
                        width: ListView.view.width - 12
                        implicitHeight: 56
                        current: isCurrent
                        queueIndex: index
                        opacity: index < panel.queue.current ? 0.5 : 1
                        onActivated: App.playback.playIndex(index)
                    }

                    Connections {
                        target: panel.queue
                        function onCurrentChanged() {
                            list.positionViewAtIndex(panel.queue.current, ListView.Contain)
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.margins: 18
                    visible: panel.queue.count === 0
                    text: qsTr("Nothing queued. Play a song, album or playlist.")
                    color: Style.textDim
                    wrapMode: Text.Wrap
                }
            }

            LyricsView {
                id: lyricsView
            }
        }
    }
}
