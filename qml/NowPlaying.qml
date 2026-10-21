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
    // a visualizer instead of the cover while no video plays
    readonly property bool showVisualizer: App.settings.visualizer !== "" && !playback.videoPlaying
    // the video draws here only with a GPU, while the view is open
    readonly property bool showVideo: (playback.videoPlaying || showVisualizer) && Style.effects && progress > 0
                                      && !Style.miniPlayer

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
        // ambient-mode: the video, much enlarged and blurred, as its own light
        MultiEffect {
            // its reach grows with the size setting (1: a tenth beyond the picture)
            readonly property real reach: App.settings.ambientSize / 100
            x: cover.x - cover.width * 0.05 * reach
            y: cover.y - cover.height * 0.1 * reach
            width: cover.width * (1 + 0.1 * reach)
            height: cover.height * (1 + 0.2 * reach)
            visible: !!video.item && video.item.ready && opacity > 0
            source: video.item ? video.item.picture : null
            // the blur runs out softly beyond the picture
            autoPaddingEnabled: true
            blurEnabled: true
            blur: 1.0
            blurMax: Math.round(64 * reach)
            saturation: 0.3
            opacity: App.settings.ambientStrength / 100
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
            Layout.preferredWidth: slot.Layout.preferredWidth
            Layout.alignment: Qt.AlignVCenter
            spacing: 14

            Item {
                Layout.fillHeight: true
            }
            // where the cover (or the video) sits when open; the cover itself flies above
            Item {
                id: slot
                readonly property bool wide: view.showVideo
                Layout.preferredWidth: wide ? Math.max(320, Math.min(view.width * 0.52, (view.height - 400) * 16 / 9))
                                            : Math.max(200, Math.min(520, view.width * 0.38, view.height - 400))
                Layout.preferredHeight: wide ? Layout.preferredWidth * 9 / 16 : Layout.preferredWidth
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: Style.slow; easing.type: Easing.OutCubic }
                }
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
            // video-toggle, its quality and captions; the visualizer
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6

                ToolButton {
                    icon.source: "icons/visualizer.svg"
                    visible: Style.effects && !view.playback.videoPlaying
                    checkable: true
                    checked: App.settings.visualizer !== ""
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Visualizer")
                    onClicked: {
                        checked = Qt.binding(() => App.settings.visualizer !== "")
                        visualizerMenu.popup(this, 0, height)
                    }
                    StyledMenu {
                        id: visualizerMenu
                        Repeater {
                            model: [
                                {id: "", name: qsTr("Off")},
                                {id: "spectrum", name: qsTr("Spectrum")},
                                {id: "bars", name: qsTr("Bars")},
                                {id: "wave", name: qsTr("Wave")},
                                {id: "scope", name: qsTr("Stereo scope")},
                            ]
                            MenuItem {
                                required property var modelData
                                text: modelData.name
                                checkable: true
                                checked: App.settings.visualizer === modelData.id
                                onTriggered: App.settings.visualizer = modelData.id
                            }
                        }
                    }
                }

                ToolButton {
                    icon.source: "icons/video.svg"
                    text: App.settings.videoMode ? qsTr("Video") : qsTr("Song")
                    display: AbstractButton.TextBesideIcon
                    checkable: true
                    checked: App.settings.videoMode
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Switch between song and music video")
                    onClicked: App.settings.videoMode = checked
                }
                ToolButton {
                    text: App.settings.videoHeight + "p"
                    visible: view.playback.videoPlaying
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Video quality")
                    onClicked: qualityMenu.popup(this, 0, height)
                    StyledMenu {
                        id: qualityMenu
                        Repeater {
                            model: [2160, 1440, 1080, 720, 480, 360]
                            MenuItem {
                                required property int modelData
                                text: modelData + "p"
                                checkable: true
                                checked: App.settings.videoHeight === modelData
                                onTriggered: App.settings.videoHeight = modelData
                            }
                        }
                    }
                }
                ToolButton {
                    icon.source: "icons/captions.svg"
                    visible: view.playback.videoPlaying && view.playback.captions.length > 0
                    checkable: true
                    checked: view.playback.caption >= 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Captions")
                    onClicked: {
                        checked = Qt.binding(() => view.playback.caption >= 0)
                        captionMenu.popup(this, 0, height)
                    }
                    StyledMenu {
                        id: captionMenu
                        MenuItem {
                            text: qsTr("Off")
                            checkable: true
                            checked: view.playback.caption < 0
                            onTriggered: {
                                view.playback.caption = -1
                                App.settings.captionLanguage = ""
                            }
                        }
                        Repeater {
                            model: view.playback.captions
                            MenuItem {
                                required property var modelData
                                required property int index
                                text: modelData.name + (modelData.generated ? " " + qsTr("(generated)") : "")
                                checkable: true
                                checked: view.playback.caption === index
                                onTriggered: {
                                    view.playback.caption = index
                                    // the next videos take this language by themselves
                                    App.settings.captionLanguage = modelData.language
                                }
                            }
                        }
                    }
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
        cover.height = mix(from.height, slot.height)
    }
    onProgressChanged: placeCover()
    // the layout moves the place without an animation running (cover to video)
    Connections {
        target: slot
        function onWidthChanged() { Qt.callLater(view.placeCover) }
        function onHeightChanged() { Qt.callLater(view.placeCover) }
        function onXChanged() { Qt.callLater(view.placeCover) }
        function onYChanged() { Qt.callLater(view.placeCover) }
    }
    Connections {
        target: view.visible ? view.Window.window : null
        function onAfterAnimating() {
            view.placeCover()
        }
    }

    Art {
        id: cover
        radius: view.mix(8, Style.radiusLarge)
        source: view.track.thumbnail || ""
        opacity: video.item && video.item.ready ? 0 : 1
        Behavior on opacity {
            NumberAnimation { duration: Style.slow }
        }
    }

    // The video over the cover's place, with its own light behind it
    // (ambient-mode: the picture, much enlarged and blurred).
    Loader {
        id: video
        active: view.showVideo
        x: cover.x
        y: cover.y
        width: cover.width
        height: cover.height

        sourceComponent: Item {
            // the first frame came
            property bool ready: false
            readonly property Item picture: picture

            MpvVideoItem {
                id: picture
                anchors.fill: parent
                player: App.player
            }
            // the cover stays until the video runs (opening it may load the
            // track again with its picture)
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
            // a visualizer switched on mid-track may not restart playback
            Timer {
                interval: 1500
                running: view.showVisualizer
                onTriggered: parent.ready = true
            }
        }
    }
}
