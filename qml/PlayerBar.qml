import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Transport, what plays, volume and the queue switch, along the bottom.
Item {
    id: bar

    readonly property MpvPlayer player: App.player
    readonly property Playback playback: App.playback
    readonly property var track: playback.track
    property bool queueOpen: false
    // the cover, where the Now Playing view grows out of
    readonly property Item artItem: cover

    // mpv reports the position ten times a second; in between it glides
    property real shownPosition: player.position
    Behavior on shownPosition {
        enabled: !bar.player.paused && Math.abs(bar.player.position - bar.shownPosition) < 1
        NumberAnimation { duration: 110 }
    }

    signal queueToggled()
    signal nowPlayingRequested()
    // opens the Now Playing view (where a video shows), never closes it
    signal nowPlayingShown()
    signal miniPlayerRequested()

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0)
            seconds = 0
        const total = Math.floor(seconds)
        const minutes = Math.floor(total / 60)
        const secs = String(total % 60).padStart(2, "0")
        if (minutes >= 60)
            return Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0") + ":" + secs
        return minutes + ":" + secs
    }

    implicitHeight: 92

    // seek line along the top edge; thicker with a knob under the pointer
    Slider {
        id: seek
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.top: parent.top
        height: 18
        padding: 0
        from: 0
        to: Math.max(bar.player.duration, 0.01)
        enabled: bar.player.duration > 0
        hoverEnabled: true

        readonly property bool active: hovered || pressed

        Binding on value {
            when: !seek.pressed
            value: bar.shownPosition
            restoreMode: Binding.RestoreNone
        }
        onPressedChanged: {
            if (!pressed)
                bar.player.seek(value)
        }

        background: Rectangle {
            x: seek.leftPadding
            y: (seek.height - height) / 2
            width: seek.availableWidth
            height: seek.active ? 6 : 3
            radius: height / 2
            color: "#26ffffff"
            Behavior on height {
                NumberAnimation { duration: Style.fast }
            }
            Rectangle {
                width: seek.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: App.settings.coloredProgress ? Style.accent : Style.text
            }
        }
        handle: Rectangle {
            x: seek.leftPadding + seek.visualPosition * seek.availableWidth - width / 2
            y: (seek.height - height) / 2
            width: 14
            height: 14
            radius: 7
            color: Style.text
            scale: seek.active ? 1 : 0
            Behavior on scale {
                NumberAnimation { duration: Style.fast; easing.type: Easing.OutBack }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 18
        anchors.topMargin: 14
        spacing: 8

        ToolButton {
            icon.source: "icons/previous.svg"
            enabled: bar.playback.queue.count > 0
            onClicked: bar.playback.previous()
        }
        RoundButton {
            id: playButton
            icon.source: bar.player.paused || bar.player.idle ? "icons/play.svg" : "icons/pause.svg"
            icon.width: 26
            icon.height: 26
            icon.color: "#111114"
            implicitWidth: 52
            implicitHeight: 52
            enabled: !bar.player.idle
            scale: pressed ? 0.92 : hovered ? 1.06 : 1
            onClicked: bar.player.togglePause()

            background: Rectangle {
                radius: width / 2
                color: playButton.enabled ? Style.accent : "#33ffffff"
                Behavior on color {
                    ColorAnimation { duration: Style.slow }
                }
            }
            Behavior on scale {
                NumberAnimation { duration: Style.fast; easing.type: Easing.OutCubic }
            }
        }
        ToolButton {
            icon.source: "icons/next.svg"
            enabled: bar.playback.hasNext
            onClicked: bar.playback.next()
        }
        Label {
            Layout.leftMargin: 6
            text: bar.formatTime(seek.pressed ? seek.value : bar.player.position) + " / "
                  + bar.formatTime(bar.player.duration)
            color: Style.textDim
            font.pixelSize: 13
            font.features: { "tnum": 1 }
        }

        Item {
            Layout.preferredWidth: 18
        }

        Art {
            id: cover
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            source: bar.track.thumbnail || ""
            visible: !!bar.track.videoId
            radius: 8
            zoom: coverHover.hovered ? 1.08 : 1

            Behavior on zoom {
                NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
            }
            HoverHandler {
                id: coverHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: bar.nowPlayingRequested()
            }

            BusyIndicator {
                anchors.centerIn: parent
                width: 36
                height: 36
                running: bar.playback.loading || bar.player.buffering
                visible: running
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: bar.track.title || ""
                color: Style.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                TapHandler {
                    onTapped: bar.nowPlayingRequested()
                }
            }
            Label {
                Layout.fillWidth: true
                text: {
                    const t = bar.track
                    // links without the underline, in the label's own color
                    const link = (id, name) => "<a href=\"" + id + "\" style=\"text-decoration:none; color:"
                        + Style.textDim + "\">" + name + "</a>"
                    const artists = (t.artists && t.artists.length)
                        ? t.artists.map(a => a.id ? link(a.id, a.name) : a.name).join(", ")
                        : (t.artistText || "")
                    const album = t.album && t.album.name
                        ? (t.album.id ? link(t.album.id, t.album.name) : t.album.name) : ""
                    return [artists, album].filter(s => !!s).join(" • ")
                }
                textFormat: Text.RichText
                linkColor: Style.textDim
                color: Style.textDim
                font.pixelSize: 13
                elide: Text.ElideRight
                onLinkActivated: link => App.openBrowse(link)
                HoverHandler {
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }

        ToolButton {
            icon.source: "icons/like.svg"
            visible: App.account.signedIn && !!bar.track.videoId
            checkable: true
            checked: bar.track.likeStatus === "LIKE"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Like")
            onClicked: App.account.rate(bar.track.videoId, checked ? "LIKE" : "INDIFFERENT")
        }
        ToolButton {
            icon.source: "icons/dislike.svg"
            visible: App.account.signedIn && !!bar.track.videoId
            checkable: true
            checked: bar.track.likeStatus === "DISLIKE"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Dislike")
            onClicked: App.account.rate(bar.track.videoId, checked ? "DISLIKE" : "INDIFFERENT")
        }

        ToolButton {
            icon.source: "icons/video.svg"
            checkable: true
            checked: App.settings.videoMode
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Switch between song and music video")
            onClicked: {
                App.settings.videoMode = checked
                // the picture is in the Now Playing view
                if (checked)
                    bar.nowPlayingShown()
            }
        }

        Label {
            text: bar.playback.streamInfo
            visible: !!bar.track.videoId && text.length > 0
            color: Style.textDim
            font.pixelSize: 11
            opacity: 0.7
        }

        // playback speed; shown when it is not 1x, the menu sets it
        ToolButton {
            text: App.effects.speed.toFixed(2).replace(/\.?0+$/, "") + "×"
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            visible: App.effects.speed !== 1
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Speed")
            onClicked: speedMenu.popup(this, 0, -speedMenu.implicitHeight)

            StyledMenu {
                id: speedMenu
                Repeater {
                    model: [0.75, 0.9, 1, 1.1, 1.25, 1.5]
                    MenuItem {
                        required property real modelData
                        text: modelData + "×"
                        checkable: true
                        checked: Math.abs(App.effects.speed - modelData) < 0.01
                        onTriggered: App.effects.speed = modelData
                    }
                }
            }
        }

        ToolButton {
            icon.source: bar.player.muted ? "icons/volume-off.svg" : "icons/volume.svg"
            onClicked: bar.player.muted = !bar.player.muted
        }

        Slider {
            id: volume
            Layout.preferredWidth: 120
            from: 0
            to: 100
            wheelEnabled: true
            stepSize: App.settings.volumeStep

            Binding on value {
                when: !volume.pressed
                value: App.settings.volume
                restoreMode: Binding.RestoreNone
            }
            onMoved: App.setVolumeSlider(value)

            ToolTip.visible: hovered || pressed
            ToolTip.text: Math.round(value) + " %"

            // right click: where the sound goes
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: point => deviceMenu.popup(volume, point.position.x, point.position.y)
            }
        }

        StyledMenu {
            id: deviceMenu
            width: 380
            onAboutToShow: App.player.watchAudioDevices()
            Repeater {
                model: App.player.audioDevices
                MenuItem {
                    required property var modelData
                    text: modelData.description || modelData.name
                    checkable: true
                    checked: App.player.audioDevice === modelData.name
                    onTriggered: App.settings.audioDevice = modelData.name
                }
            }
        }

        ToolButton {
            icon.source: "icons/queue.svg"
            checkable: true
            checked: bar.queueOpen
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Up next")
            onClicked: bar.queueToggled()
        }
        ToolButton {
            icon.source: "icons/miniplayer.svg"
            enabled: !!bar.track.videoId
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Mini player")
            onClicked: bar.miniPlayerRequested()
        }
        ToolButton {
            icon.source: "icons/chevron-left.svg"
            rotation: 90
            enabled: !!bar.track.videoId
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Now playing")
            onClicked: bar.nowPlayingRequested()
        }
    }
}
