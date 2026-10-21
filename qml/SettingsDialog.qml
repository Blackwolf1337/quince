import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
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
    // a label and a box of options [{id, name}]
    component Choice: RowLayout {
        id: choice
        property alias text: choiceLabel.text
        property var options: []
        property var value
        signal chosen(var value)
        Layout.fillWidth: true
        Label {
            id: choiceLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        ComboBox {
            Layout.preferredWidth: 230
            textRole: "name"
            valueRole: "id"
            model: choice.options
            // count: the options may be set after the box was built
            currentIndex: count > 0 ? indexOfValue(choice.value) : -1
            onActivated: choice.chosen(currentValue)
        }
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
            Toggle {
                text: qsTr("Skip the parts of music videos without music (SponsorBlock)")
                checked: dialog.settings.sponsorBlock
                onToggled: dialog.settings.sponsorBlock = checked
            }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 52
                spacing: 6
                visible: dialog.settings.sponsorBlock
                Repeater {
                    model: [
                        {id: "music_offtopic", name: qsTr("Without music")},
                        {id: "sponsor", name: qsTr("Sponsors")},
                        {id: "selfpromo", name: qsTr("Self promotion")},
                        {id: "interaction", name: qsTr("Subscribe reminders")},
                        {id: "intro", name: qsTr("Intros")},
                        {id: "outro", name: qsTr("Outros")},
                    ]
                    delegate: CheckBox {
                        required property var modelData
                        text: modelData.name
                        checked: dialog.settings.sponsorBlockCategories.indexOf(modelData.id) >= 0
                        onToggled: {
                            const list = dialog.settings.sponsorBlockCategories.filter(c => c !== modelData.id)
                            if (checked)
                                list.push(modelData.id)
                            dialog.settings.sponsorBlockCategories = list
                        }
                    }
                }
            }
            Toggle {
                text: qsTr("Continue the last queue at start")
                checked: dialog.settings.resumeSession
                onToggled: dialog.settings.resumeSession = checked
            }
            Toggle {
                text: qsTr("Skip songs I disliked")
                checked: dialog.settings.skipDisliked
                onToggled: dialog.settings.skipDisliked = checked
            }
            Toggle {
                text: qsTr("Songs I pick start paused")
                checked: dialog.settings.startPaused
                onToggled: dialog.settings.startPaused = checked
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
                    currentIndex: count > 0 ? indexOfValue(App.player.audioDevice) : -1
                    onActivated: dialog.settings.audioDevice = currentValue
                }
            }

            Section {
                text: qsTr("Sound")
            }
            RowLayout {
                Layout.fillWidth: true
                Toggle {
                    text: qsTr("Equalizer")
                    checked: App.effects.equalizer
                    onToggled: App.effects.equalizer = checked
                }
                Button {
                    text: App.effects.presetName(App.effects.preset)
                    flat: true
                    onClicked: App.requestDialog("equalizer")
                }
            }
            Toggle {
                text: qsTr("Compressor: quiet parts louder, loud parts quieter")
                checked: App.effects.compressor
                onToggled: App.effects.compressor = checked
            }
            Toggle {
                text: qsTr("Skip silences")
                checked: App.effects.skipSilences
                onToggled: App.effects.skipSilences = checked
            }
            Toggle {
                text: qsTr("Only the silence at the start of a track")
                enabled: App.effects.skipSilences
                checked: App.effects.skipSilencesAtStart
                onToggled: App.effects.skipSilencesAtStart = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Crossfade")
                }
                Slider {
                    id: crossfadeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 12
                    stepSize: 1
                    value: App.effects.crossfade
                    onMoved: App.effects.crossfade = value
                }
                Label {
                    Layout.preferredWidth: 72
                    text: crossfadeSlider.value > 0 ? qsTr("%1 s").arg(crossfadeSlider.value) : qsTr("Off (gapless)")
                    color: Style.textDim
                }
            }
            RowLayout {
                Layout.fillWidth: true
                enabled: App.effects.crossfade > 0
                Label {
                    text: qsTr("Fade in")
                }
                Slider {
                    id: fadeInSlider
                    Layout.fillWidth: true
                    from: 0
                    to: App.effects.crossfade
                    stepSize: 0.5
                    value: App.effects.crossfadeIn
                    onMoved: App.effects.crossfadeIn = value
                }
                Label {
                    Layout.preferredWidth: 72
                    text: fadeInSlider.value > 0 ? qsTr("%1 s").arg(fadeInSlider.value) : qsTr("As long")
                    color: Style.textDim
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Speed")
                }
                Slider {
                    id: speedSlider
                    Layout.fillWidth: true
                    from: 0.5
                    to: 2
                    stepSize: 0.05
                    value: App.effects.speed
                    onMoved: App.effects.speed = value
                }
                Label {
                    Layout.preferredWidth: 72
                    text: speedSlider.value.toFixed(2) + "×"
                    color: Style.textDim
                }
            }

            Section {
                text: qsTr("Downloads")
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Format")
                }
                ComboBox {
                    Layout.fillWidth: true
                    textRole: "name"
                    valueRole: "id"
                    model: [
                        {id: "opus", name: qsTr("Opus (as YouTube sends it, best quality)")},
                        {id: "m4a", name: qsTr("M4A / AAC (as YouTube sends it)")},
                        {id: "mp3", name: qsTr("MP3 (converted)")},
                    ]
                    currentIndex: count > 0 ? indexOfValue(dialog.settings.downloadFormat) : -1
                    onActivated: dialog.settings.downloadFormat = currentValue
                }
            }
            Choice {
                visible: dialog.settings.downloadFormat === "mp3"
                text: qsTr("MP3 bit rate")
                options: [
                    {id: 128, name: "128 kbit/s"},
                    {id: 192, name: "192 kbit/s"},
                    {id: 256, name: "256 kbit/s"},
                    {id: 320, name: "320 kbit/s"},
                ]
                value: dialog.settings.downloadMp3Kbps
                onChosen: value => dialog.settings.downloadMp3Kbps = value
            }
            Toggle {
                text: qsTr("Skip tracks that are already in the folder")
                checked: dialog.settings.downloadSkipExisting
                onToggled: dialog.settings.downloadSkipExisting = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Tracks per playlist at most (0 all)")
                }
                SpinBox {
                    from: 0
                    to: 5000
                    stepSize: 10
                    editable: true
                    value: dialog.settings.downloadPlaylistMax
                    onValueModified: dialog.settings.downloadPlaylistMax = value
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: dialog.settings.downloadFolder
                    onEditingFinished: dialog.settings.downloadFolder = text
                }
                Button {
                    text: qsTr("Choose")
                    flat: true
                    onClicked: folderDialog.open()
                }
                FolderDialog {
                    id: folderDialog
                    currentFolder: "file://" + dialog.settings.downloadFolder
                    onAccepted: dialog.settings.downloadFolder = decodeURIComponent(selectedFolder.toString().replace("file://", ""))
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
            Toggle {
                text: qsTr("Time-synced lyrics from Megalobiz")
                checked: dialog.settings.lyricsMegalobiz
                onToggled: dialog.settings.lyricsMegalobiz = checked
            }
            Toggle {
                text: qsTr("Lyrics from Genius when there are none else")
                checked: dialog.settings.lyricsGenius
                onToggled: dialog.settings.lyricsGenius = checked
            }
            Choice {
                text: qsTr("Ask first")
                options: [
                    {id: "", name: qsTr("No preference")},
                    {id: "lrclib", name: "LRCLIB"},
                    {id: "megalobiz", name: "Megalobiz"},
                    {id: "youtube", name: "YouTube Music"},
                    {id: "genius", name: "Genius"},
                ]
                value: dialog.settings.lyricsPreferred
                onChosen: value => dialog.settings.lyricsPreferred = value
            }
            Toggle {
                text: qsTr("Show lyrics even if the match is inexact")
                checked: dialog.settings.lyricsInexact
                onToggled: dialog.settings.lyricsInexact = checked
            }
            Choice {
                text: qsTr("The sung line")
                options: [
                    {id: "fancy", name: qsTr("Fancy: large, with a glow")},
                    {id: "scale", name: qsTr("Scale: grows")},
                    {id: "offset", name: qsTr("Offset: moves to the right")},
                    {id: "focus", name: qsTr("Focus: only it is bright")},
                ]
                value: dialog.settings.lyricsEffect
                onChosen: value => dialog.settings.lyricsEffect = value
            }
            Toggle {
                text: qsTr("Time codes next to the lines")
                checked: dialog.settings.lyricsTimeCodes
                onToggled: dialog.settings.lyricsTimeCodes = checked
            }
            Toggle {
                text: qsTr("Latin letters under Japanese, Korean and Chinese lyrics")
                checked: dialog.settings.lyricsRomanize
                onToggled: dialog.settings.lyricsRomanize = checked
            }
            Choice {
                text: qsTr("Chinese characters")
                options: [
                    {id: "", name: qsTr("As they come")},
                    {id: "hant", name: qsTr("Traditional")},
                    {id: "hans", name: qsTr("Simplified")},
                ]
                value: dialog.settings.lyricsChinese
                onChosen: value => dialog.settings.lyricsChinese = value
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Mark for pauses between lines")
                }
                TextField {
                    Layout.preferredWidth: 80
                    horizontalAlignment: Text.AlignHCenter
                    text: dialog.settings.lyricsGapText
                    onEditingFinished: dialog.settings.lyricsGapText = text
                }
            }

            Section {
                text: qsTr("Look")
            }
            Choice {
                text: qsTr("Language")
                options: [
                    {id: "", name: qsTr("As the system")},
                    {id: "en", name: "English"},
                    {id: "de", name: "Deutsch"},
                ]
                value: dialog.settings.language
                onChosen: value => dialog.settings.language = value
            }
            Toggle {
                text: qsTr("Colors from the album cover")
                checked: dialog.settings.albumColors
                onToggled: dialog.settings.albumColors = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Strength of the glow")
                }
                Slider {
                    id: glowStrengthSlider
                    Layout.preferredWidth: 200
                    from: 0
                    to: 200
                    stepSize: 10
                    value: dialog.settings.glowStrength
                    onMoved: dialog.settings.glowStrength = value
                }
                Label {
                    Layout.preferredWidth: 48
                    text: Math.round(glowStrengthSlider.value) + " %"
                    color: Style.textDim
                }
            }
            Toggle {
                text: qsTr("Progress bar in the album color")
                checked: dialog.settings.coloredProgress
                onToggled: dialog.settings.coloredProgress = checked
            }
            Choice {
                text: qsTr("Start page")
                options: [
                    {id: "home", name: qsTr("Home")},
                    {id: "explore", name: qsTr("Explore")},
                    {id: "library", name: qsTr("Library")},
                    {id: "liked", name: qsTr("Liked songs")},
                ]
                value: dialog.settings.startPage
                onChosen: value => dialog.settings.startPage = value
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
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 52
                visible: dialog.settings.clock
                CheckBox {
                    text: qsTr("Seconds")
                    checked: dialog.settings.clockSeconds
                    onToggled: dialog.settings.clockSeconds = checked
                }
                ComboBox {
                    Layout.fillWidth: true
                    textRole: "name"
                    valueRole: "id"
                    model: [
                        {id: "", name: qsTr("As the system")},
                        {id: "24", name: qsTr("24 hours")},
                        {id: "12", name: qsTr("12 hours")},
                    ]
                    currentIndex: count > 0 ? indexOfValue(dialog.settings.clockHours) : -1
                    onActivated: dialog.settings.clockHours = currentValue
                }
            }
            Toggle {
                text: qsTr("See-through window, for the desktop's blur (after a restart)")
                checked: dialog.settings.transparentWindow
                onToggled: dialog.settings.transparentWindow = checked
            }
            RowLayout {
                Layout.fillWidth: true
                enabled: dialog.settings.transparentWindow
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Window background")
                }
                Slider {
                    id: windowOpacitySlider
                    Layout.preferredWidth: 200
                    from: 0
                    to: 100
                    stepSize: 5
                    value: dialog.settings.windowOpacity
                    onMoved: dialog.settings.windowOpacity = value
                }
                Label {
                    Layout.preferredWidth: 48
                    text: Math.round(windowOpacitySlider.value) + " %"
                    color: Style.textDim
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Ambient light of videos")
                }
                Slider {
                    id: ambientStrengthSlider
                    Layout.preferredWidth: 200
                    from: 0
                    to: 100
                    stepSize: 5
                    value: dialog.settings.ambientStrength
                    onMoved: dialog.settings.ambientStrength = value
                }
                Label {
                    Layout.preferredWidth: 48
                    text: Math.round(ambientStrengthSlider.value) + " %"
                    color: Style.textDim
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Reach of the ambient light")
                }
                Slider {
                    id: ambientSizeSlider
                    Layout.preferredWidth: 200
                    from: 50
                    to: 200
                    stepSize: 10
                    value: dialog.settings.ambientSize
                    onMoved: dialog.settings.ambientSize = value
                }
                Label {
                    Layout.preferredWidth: 48
                    text: Math.round(ambientSizeSlider.value) + " %"
                    color: Style.textDim
                }
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
                text: qsTr("A click on the tray icon plays or pauses")
                enabled: dialog.settings.tray
                checked: dialog.settings.trayClickPlays
                onToggled: dialog.settings.trayClickPlays = checked
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
            Toggle {
                text: qsTr("Also when playback goes on after a pause")
                enabled: dialog.settings.notifications
                checked: dialog.settings.notifyOnResume
                onToggled: dialog.settings.notifyOnResume = checked
            }
            Choice {
                visible: Qt.platform.os === "linux"
                enabled: dialog.settings.notifications
                text: qsTr("Urgency of the notification")
                options: [
                    {id: "low", name: qsTr("Low")},
                    {id: "normal", name: qsTr("Normal")},
                    {id: "critical", name: qsTr("Critical")},
                ]
                value: dialog.settings.notificationUrgency
                onChosen: value => dialog.settings.notificationUrgency = value
            }

            Section {
                text: qsTr("Discord")
            }
            Toggle {
                text: qsTr("Show what I listen to in Discord")
                checked: dialog.settings.discord
                onToggled: dialog.settings.discord = checked
            }
            Choice {
                enabled: dialog.settings.discord
                text: qsTr("The member list shows")
                options: [
                    {id: "app", name: qsTr("The app's name")},
                    {id: "artist", name: qsTr("The artist")},
                    {id: "title", name: qsTr("The title")},
                ]
                value: dialog.settings.discordShows
                onChosen: value => dialog.settings.discordShows = value
            }
            Toggle {
                enabled: dialog.settings.discord
                text: qsTr("Show the time left")
                checked: !dialog.settings.discordHideTimeLeft
                onToggled: dialog.settings.discordHideTimeLeft = !checked
            }
            Toggle {
                enabled: dialog.settings.discord
                text: qsTr("Button \"Listen on YouTube Music\"")
                checked: dialog.settings.discordButton
                onToggled: dialog.settings.discordButton = checked
            }
            RowLayout {
                Layout.fillWidth: true
                enabled: dialog.settings.discord
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Clear the status after a pause of (minutes, 0 never)")
                }
                SpinBox {
                    from: 0
                    to: 120
                    value: dialog.settings.discordPauseMinutes
                    onValueModified: dialog.settings.discordPauseMinutes = value
                }
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
