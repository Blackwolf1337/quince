import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
import Quince

// The playing track over the whole window: a large cover with its light, the
// controls, and lyrics or the queue beside it. The cover grows out of the
// player bar when it opens and goes back there when it closes.
Item {
    id: view

    readonly property MpvPlayer player: App.player
    readonly property Playback playback: App.playback
    readonly property var track: playback.track

    property bool open: false
    // 0 closed .. 1 open
    property real progress: 0
    // where the cover starts from, in this item
    property rect from: Qt.rect(width / 2, height, 0, 0)

    function show(startItem) {
        if (!track.videoId)
            return
        if (startItem && startItem.visible)
            from = startItem.mapToItem(view, 0, 0, startItem.width, startItem.height)
        open = true
        motion.to = 1
        motion.restart()
    }

    function hide() {
        if (!open)
            return
        open = false
        motion.to = 0
        motion.restart()
    }

    function mix(a, b) {
        return a + (b - a) * progress
    }

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0)
            seconds = 0
        const total = Math.floor(seconds)
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
    }

    visible: progress > 0
    // the pages behind take no input while it is open
    enabled: open

    NumberAnimation {
        id: motion
        target: view
        property: "progress"
        duration: Style.slow + 60
        easing.type: Easing.OutCubic
    }

    Connections {
        target: view.playback
        function onTrackChanged() {
            if (!view.track.videoId)
                view.hide()
        }
    }

    // swallows clicks and wheel so nothing behind reacts
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    // the cover, blurred over the whole window, darkened
    Item {
        anchors.fill: parent
        opacity: view.progress

        Rectangle {
            anchors.fill: parent
            color: Style.background
        }
        Art {
            id: blurSource
            width: 96
            height: 96
            radius: 0
            source: view.track.thumbnail || ""
            visible: false
        }
        MultiEffect {
            anchors.centerIn: parent
            width: Math.max(parent.width, parent.height) * 1.15
            height: width
            source: blurSource
            visible: Style.effects
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 32
            saturation: 0.25
            brightness: -0.35
            opacity: 0.85
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.alpha(Style.background, 0.35) }
                GradientStop { position: 1.0; color: Qt.alpha(Style.background, 0.85) }
            }
        }
        // light behind the cover
        Glow {
            width: Math.max(cover.width * 2.4, 400)
            height: width
            x: cover.x + cover.width / 2 - width / 2
            y: cover.y + cover.height / 2 - height / 2
            strength: 0.55
        }
    }

    ToolButton {
        x: 16
        y: 14
        icon.source: "icons/chevron-left.svg"
        rotation: -90
        opacity: view.progress
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Close")
        onClicked: view.hide()
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: 64
        anchors.rightMargin: 48
        anchors.topMargin: 64
        anchors.bottomMargin: 40
        spacing: 56
        opacity: Math.max(0, view.progress * 1.4 - 0.4)
        transform: Translate {
            y: (1 - view.progress) * 40
        }

        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: slot.width
            Layout.alignment: Qt.AlignVCenter
            spacing: 14

            Item {
                Layout.fillHeight: true
            }
            // where the cover sits when open (the cover itself flies above)
            Item {
                id: slot
                Layout.preferredWidth: Math.max(200, Math.min(520, view.width * 0.38, view.height - 400))
                Layout.preferredHeight: Layout.preferredWidth
            }
            Label {
                Layout.topMargin: 14
                Layout.fillWidth: true
                text: view.track.title || ""
                color: Style.text
                font.pixelSize: 28
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: {
                    const t = view.track
                    const artists = (t.artists && t.artists.length) ? t.artists.map(a => a.name).join(", ") : (t.artistText || "")
                    return [artists, t.album && t.album.name ? t.album.name : ""].filter(s => !!s).join(" • ")
                }
                color: Style.textDim
                font.pixelSize: 16
                elide: Text.ElideRight
            }

            Slider {
                id: seek
                Layout.fillWidth: true
                Layout.topMargin: 8
                from: 0
                to: Math.max(view.player.duration, 0.01)
                enabled: view.player.duration > 0
                Binding on value {
                    when: !seek.pressed
                    value: view.player.position
                    restoreMode: Binding.RestoreNone
                }
                onPressedChanged: {
                    if (!pressed)
                        view.player.seek(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -10
                Label {
                    text: view.formatTime(seek.pressed ? seek.value : view.player.position)
                    color: Style.textDim
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: view.formatTime(view.player.duration)
                    color: Style.textDim
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 18

                ToolButton {
                    icon.source: "icons/dislike.svg"
                    visible: App.account.signedIn
                    checkable: true
                    checked: view.track.likeStatus === "DISLIKE"
                    onClicked: App.account.rate(view.track.videoId, checked ? "DISLIKE" : "INDIFFERENT")
                }
                ToolButton {
                    icon.source: "icons/previous.svg"
                    icon.width: 28
                    icon.height: 28
                    onClicked: view.playback.previous()
                }
                RoundButton {
                    id: play
                    icon.source: view.player.paused || view.player.idle ? "icons/play.svg" : "icons/pause.svg"
                    icon.width: 32
                    icon.height: 32
                    icon.color: "#111114"
                    implicitWidth: 72
                    implicitHeight: 72
                    scale: pressed ? 0.92 : hovered ? 1.05 : 1
                    onClicked: view.player.togglePause()
                    background: Rectangle {
                        radius: width / 2
                        color: Style.accent
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Style.fast; easing.type: Easing.OutCubic }
                    }
                }
                ToolButton {
                    icon.source: "icons/next.svg"
                    icon.width: 28
                    icon.height: 28
                    enabled: view.playback.hasNext
                    onClicked: view.playback.next()
                }
                ToolButton {
                    icon.source: "icons/like.svg"
                    visible: App.account.signedIn
                    checkable: true
                    checked: view.track.likeStatus === "LIKE"
                    onClicked: App.account.rate(view.track.videoId, checked ? "LIKE" : "INDIFFERENT")
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }

        QueuePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 320
            tab: 1
            lyricsSize: 30
            plainLyricsSize: 19
        }
    }

    // The cover, on its way between the player bar and its place. Placed by
    // hand on every frame: the layout moves its place without telling.
    function placeCover() {
        const target = slot.mapToItem(view, 0, 0)
        cover.x = mix(from.x, target.x)
        cover.y = mix(from.y, target.y)
        cover.width = mix(from.width, slot.width)
    }
    onProgressChanged: placeCover()
    Connections {
        target: view.visible ? view.Window.window : null
        function onAfterAnimating() {
            view.placeCover()
        }
    }

    Art {
        id: cover
        height: width
        radius: view.mix(8, Style.radiusLarge)
        source: view.track.thumbnail || ""
    }
}
