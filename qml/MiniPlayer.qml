import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
import Quince

// picture-in-picture: a small window with the cover (or the video), what
// plays and the transport, which show under the pointer. Wayland lets no
// window keep itself on top; on Hyprland a rule on the title does
// ("pin" and "float" for title "Quince mini player").
Window {
    id: mini

    readonly property MpvPlayer player: App.player
    readonly property Playback playback: App.playback
    readonly property var track: playback.track
    readonly property bool showVideo: visible && Style.effects
        && (playback.videoPlaying || (App.settings.visualizer !== "" && !playback.videoPlaying))

    signal restoreRequested()

    // not translated: window rules match it
    title: "Quince mini player"
    width: 340
    height: showVideo && playback.videoPlaying ? 240 : 340
    minimumWidth: 220
    minimumHeight: 160
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Style.background

    Material.theme: Material.Dark
    Material.accent: Style.accent

    Art {
        id: cover
        anchors.fill: parent
        radius: 0
        source: mini.track.thumbnail || ""
        visible: !mini.showVideo || !(video.item && video.item.ready)
    }

    Loader {
        id: video
        anchors.fill: parent
        active: mini.showVideo
        sourceComponent: Item {
            property bool ready: false
            MpvVideoItem {
                anchors.fill: parent
                player: App.player
            }
            Connections {
                target: App.player
                function onPlaybackRestarted() {
                    readyTimer.restart()
                }
            }
            Timer {
                id: readyTimer
                interval: 250
                onTriggered: parent.ready = true
            }
            Timer {
                interval: 1500
                running: !mini.playback.videoPlaying
                onTriggered: parent.ready = true
            }
        }
    }

    // the window moves when dragged anywhere
    DragHandler {
        target: null
        onActiveChanged: {
            if (active)
                mini.startSystemMove()
        }
    }
    HoverHandler {
        id: hover
    }

    // what plays and the controls, over a shade, under the pointer
    Item {
        anchors.fill: parent
        opacity: hover.hovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: Style.normal }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#99000000" }
                GradientStop { position: 0.35; color: "#22000000" }
                GradientStop { position: 0.6; color: "#22000000" }
                GradientStop { position: 1.0; color: "#cc000000" }
            }
        }

        RowLayout {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8
            ToolButton {
                icon.source: "icons/miniplayer.svg"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to the full window")
                onClicked: mini.restoreRequested()
            }
            Item {
                Layout.fillWidth: true
            }
            ToolButton {
                icon.source: mini.playback.track.likeStatus === "LIKE" ? "icons/heart.svg" : "icons/like.svg"
                visible: App.account.signedIn && !!mini.track.videoId
                onClicked: App.account.rate(mini.track.videoId, mini.track.likeStatus === "LIKE" ? "INDIFFERENT" : "LIKE")
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: mini.track.title || ""
                color: "white"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: (mini.track.artists && mini.track.artists.length)
                      ? mini.track.artists.map(a => a.name).join(", ") : (mini.track.artistText || "")
                color: "#ccffffff"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                spacing: 10
                ToolButton {
                    icon.source: "icons/previous.svg"
                    onClicked: mini.playback.previous()
                }
                RoundButton {
                    icon.source: mini.player.paused || mini.player.idle ? "icons/play.svg" : "icons/pause.svg"
                    icon.color: "#111114"
                    implicitWidth: 48
                    implicitHeight: 48
                    onClicked: mini.player.togglePause()
                    background: Rectangle {
                        radius: width / 2
                        color: Style.accent
                    }
                }
                ToolButton {
                    icon.source: "icons/next.svg"
                    enabled: mini.playback.hasNext
                    onClicked: mini.playback.next()
                }
            }
        }
    }

    // progress along the bottom edge, always there
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: 3
        width: parent.width * (mini.player.duration > 0 ? mini.player.position / mini.player.duration : 0)
        color: Style.accent
    }

    Shortcut {
        sequence: "Space"
        onActivated: mini.player.togglePause()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: mini.restoreRequested()
    }
}
