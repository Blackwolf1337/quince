import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Transport, what plays, volume and the queue switch, along the bottom.
Rectangle {
    id: bar

    readonly property MpvPlayer player: App.player
    readonly property Playback playback: App.playback
    readonly property var track: playback.track
    property bool queueOpen: false

    signal queueToggled()

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

    implicitHeight: 84
    color: Style.surface

    // seek line along the top edge
    Slider {
        id: seek
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.top
        height: 24
        padding: 0
        from: 0
        to: Math.max(bar.player.duration, 0.01)
        enabled: bar.player.duration > 0

        Binding on value {
            when: !seek.pressed
            value: bar.player.position
            restoreMode: Binding.RestoreNone
        }
        onPressedChanged: {
            if (!pressed)
                bar.player.seek(value)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 16
        anchors.topMargin: 6
        spacing: 8

        ToolButton {
            icon.source: "icons/previous.svg"
            enabled: bar.playback.queue.count > 0
            onClicked: bar.playback.previous()
        }
        RoundButton {
            icon.source: bar.player.paused || bar.player.idle ? "icons/play.svg" : "icons/pause.svg"
            icon.width: 28
            icon.height: 28
            implicitWidth: 56
            implicitHeight: 56
            highlighted: true
            enabled: !bar.player.idle
            onClicked: bar.player.togglePause()
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
            Layout.preferredWidth: 52
            Layout.preferredHeight: 52
            source: bar.track.thumbnail || ""
            visible: !!bar.track.videoId

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
            }
            Label {
                Layout.fillWidth: true
                text: {
                    const t = bar.track
                    const artists = (t.artists && t.artists.length)
                        ? t.artists.map(a => a.id ? "<a href=\"" + a.id + "\">" + a.name + "</a>" : a.name).join(", ")
                        : (t.artistText || "")
                    const album = t.album && t.album.name
                        ? (t.album.id ? "<a href=\"" + t.album.id + "\">" + t.album.name + "</a>" : t.album.name) : ""
                    return [artists, album].filter(s => !!s).join(" • ")
                }
                textFormat: Text.StyledText
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
            icon.source: "icons/dislike.svg"
            visible: App.account.signedIn && !!bar.track.videoId
            checkable: true
            checked: bar.track.likeStatus === "DISLIKE"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Dislike")
            onClicked: App.account.rate(bar.track.videoId, checked ? "DISLIKE" : "INDIFFERENT")
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

        Label {
            text: bar.playback.streamInfo
            visible: !!bar.track.videoId && text.length > 0
            color: Style.textDim
            font.pixelSize: 11
            opacity: 0.7
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
            onClicked: bar.queueToggled()
        }
    }
}
