import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

Dialog {
    id: dialog

    readonly property Settings settings: App.settings

    component Section: Label {
        Layout.topMargin: 14
        font.pixelSize: 13
        font.weight: Font.Bold
        font.capitalization: Font.AllUppercase
        color: Style.textDim
    }
    component Toggle: Switch {
        Layout.fillWidth: true
    }

    title: qsTr("Settings")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 48 : 560)
    height: Math.min(680, Overlay.overlay ? Overlay.overlay.height - 48 : 680)
    standardButtons: Dialog.Close
    onClosed: destroy()
    Component.onCompleted: App.player.watchAudioDevices()

    // title and button bar lie on the glass too
    Material.background: "transparent"
    background: Glass {
        radius: Style.radiusLarge
    }
    Overlay.modal: GlassScrim {}
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal }
            NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Style.slow; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Style.fast }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 4

            Section {
                Layout.topMargin: 0
                text: qsTr("Playback")
            }
            Toggle {
                text: qsTr("Volume slider follows your hearing (exponential)")
                checked: dialog.settings.exponentialVolume
                onToggled: dialog.settings.exponentialVolume = checked
            }
            Toggle {
                text: qsTr("Even out loudness between tracks, like YouTube")
                checked: dialog.settings.normalizeLoudness
                onToggled: dialog.settings.normalizeLoudness = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Volume step of wheel and keys (%)")
                }
                SpinBox {
                    from: 1
                    to: 10
                    value: dialog.settings.volumeStep
                    onValueModified: dialog.settings.volumeStep = value
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Output")
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: App.player.audioDevices
                    textRole: "description"
                    valueRole: "name"
                    // the device list may arrive after the box was built
                    currentIndex: count, indexOfValue(App.player.audioDevice)
                    onActivated: dialog.settings.audioDevice = currentValue
                }
            }

            Section {
                text: qsTr("Lyrics")
            }
            Toggle {
                text: qsTr("Time-synced lyrics from LRCLIB")
                checked: dialog.settings.lyricsLrclib
                onToggled: dialog.settings.lyricsLrclib = checked
            }

            Section {
                text: qsTr("Look")
            }
            Toggle {
                text: qsTr("Colors from the album cover")
                checked: dialog.settings.albumColors
                onToggled: dialog.settings.albumColors = checked
            }
            Toggle {
                text: qsTr("Compact sidebar (icons only)")
                checked: dialog.settings.compactSidebar
                onToggled: dialog.settings.compactSidebar = checked
            }
            Toggle {
                text: qsTr("Clock in the top bar")
                checked: dialog.settings.clock
                onToggled: dialog.settings.clock = checked
            }
            Toggle {
                text: qsTr("See-through window, for the desktop's blur (after a restart)")
                checked: dialog.settings.transparentWindow
                onToggled: dialog.settings.transparentWindow = checked
            }

            Section {
                text: qsTr("Desktop")
            }
            Toggle {
                text: qsTr("Icon in the system tray")
                checked: dialog.settings.tray
                onToggled: dialog.settings.tray = checked
            }
            Toggle {
                text: qsTr("Closing the window keeps playing in the tray")
                enabled: dialog.settings.tray
                checked: dialog.settings.closeToTray
                onToggled: dialog.settings.closeToTray = checked
            }
            Toggle {
                text: qsTr("System-wide shortcuts (desktop portal, after a restart)")
                visible: Qt.platform.os === "linux"
                checked: dialog.settings.globalShortcuts
                onToggled: dialog.settings.globalShortcuts = checked
            }
            Toggle {
                text: qsTr("Notification when the track changes")
                checked: dialog.settings.notifications
                onToggled: dialog.settings.notifications = checked
            }

            Section {
                text: qsTr("Discord")
            }
            Toggle {
                text: qsTr("Show what I listen to in Discord")
                checked: dialog.settings.discord
                onToggled: dialog.settings.discord = checked
            }
            TextField {
                Layout.fillWidth: true
                enabled: dialog.settings.discord
                placeholderText: qsTr("Application ID")
                text: dialog.settings.discordAppId
                onEditingFinished: dialog.settings.discordAppId = text
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Style.textDim
                font.pixelSize: 12
                text: qsTr("Uses Pear Desktop's Discord application. For your own, create one at "
                           + "discord.com/developers/applications and paste its Application ID; leave empty for Pear's.")
            }
        }
    }
}
