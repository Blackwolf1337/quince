import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

ApplicationWindow {
    id: window

    property string section: "home"
    property bool queueOpen: false
    // the settings category last open, for this session
    property string settingsPage: "playback"
    // pages left by going back, for going forward again
    property var forwardPages: []

    function goSection(name, browseId) {
        section = name
        forwardPages = []
        if (name === "downloads")
            stack.replace(null, downloadsComponent)
        else
            stack.replace(null, browseComponent, {browseId: browseId || ""})
    }

    function openPage(properties) {
        forwardPages = []
        stack.push(browseComponent, properties)
    }

    function showToast(text, isError) {
        toast.message = ""
        toast.error = isError
        toast.message = text
    }

    function search(query) {
        openPage({query: query})
    }

    // the mini player takes the window's place, and gives it back
    function openMiniPlayer() {
        Style.miniPlayer = true
        miniPlayer.show()
        window.hide()
    }
    function closeMiniPlayer() {
        window.show()
        window.raise()
        window.requestActivate()
        miniPlayer.hide()
        Style.miniPlayer = false
    }

    function back() {
        if (nowPlaying.open) {
            nowPlaying.hide()
            return
        }
        if (stack.depth <= 1)
            return
        const page = stack.currentItem
        if (page.browseId !== undefined)
            forwardPages = forwardPages.concat([{browseId: page.browseId, params: page.params, query: page.query}])
        stack.pop()
    }

    function forward() {
        if (forwardPages.length === 0)
            return
        const page = forwardPages[forwardPages.length - 1]
        forwardPages = forwardPages.slice(0, -1)
        stack.push(browseComponent, page)
    }

    // the start section, and after signing in or out
    function goStart() {
        const page = App.settings.startPage
        const ids = {explore: "FEmusic_explore", library: "FEmusic_library_landing", liked: "VLLM"}
        if (page in ids && (page === "explore" || App.account.signedIn))
            goSection(page, ids[page])
        else
            goSection("home", "")
    }

    // the last size; the compositor may still choose another
    width: Math.max(minimumWidth, App.settings.savedWindowSize().width)
    height: Math.max(minimumHeight, App.settings.savedWindowSize().height)
    onWidthChanged: sizeSaver.restart()
    onHeightChanged: sizeSaver.restart()
    minimumWidth: 820
    minimumHeight: 520
    visible: true
    color: App.settings.transparentWindow ? "transparent" : Style.background
    font.family: Style.font
    title: App.playback.track.title ? App.playback.track.title + " - Quince" : "Quince"

    // with the tray on and closeToTray set, closing only hides the window
    onClosing: close => {
        if (App.settings.tray && App.settings.closeToTray) {
            close.accepted = false
            window.hide()
        }
    }

    Material.theme: Material.Dark
    Material.accent: Style.accent
    Material.primary: Style.surface
    Material.background: Style.surface

    Component.onCompleted: {
        Style.backdrop = scene
        Style.effects = GraphicsInfo.api !== GraphicsInfo.Software
        if (App.settings.startPage !== "home")
            goStart()
    }

    Timer {
        id: sizeSaver
        interval: 600
        onTriggered: {
            // a maximized or full screen window keeps the size it had before
            if (window.visibility !== Window.Windowed)
                return
            App.settings.windowWidth = window.width
            App.settings.windowHeight = window.height
        }
    }

    Connections {
        target: App
        function onNavigateRequested(target) {
            nowPlaying.hide()
            window.openPage({browseId: target.browseId, params: target.params || ""})
        }
        function onDialogRequested(name, data) {
            if (name === "signin")
                signInComponent.createObject(window.contentItem).open()
            else if (name === "addToPlaylist")
                playlistPickerComponent.createObject(window.contentItem, {item: data.item}).open()
            else if (name === "settings") {
                const settings = settingsComponent.createObject(window.contentItem, {page: window.settingsPage})
                settings.pageChanged.connect(() => window.settingsPage = settings.page)
                settings.open()
            }
            else if (name === "equalizer")
                equalizerComponent.createObject(window.contentItem).open()
        }
    }
    Connections {
        target: App.account
        // pages and the home cache belong to the account they were loaded for
        function onSignedInChanged() {
            window.goStart()
        }
        function onMessage(text) {
            window.showToast(text, false)
        }
    }
    Connections {
        target: App
        function onMessage(text) {
            window.showToast(text, false)
        }
    }
    Connections {
        target: App.sponsorBlock
        function onSkipped(text) {
            window.showToast(text, false)
        }
    }
    Connections {
        target: App.playback
        function onErrorChanged() {
            if (App.playback.error)
                window.showToast(App.playback.error, true)
        }
    }
    Connections {
        target: App.settings
        function onChanged() {
            if (Math.abs(App.settings.volume - volumeHud.value) > 0.01) {
                volumeHud.value = App.settings.volume
                volumeHud.show()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Back, "Alt+Left"]
        onActivated: window.back()
    }
    Shortcut {
        sequences: [StandardKey.Forward, "Alt+Right"]
        onActivated: window.forward()
    }
    Shortcut {
        sequence: "Escape"
        enabled: nowPlaying.open
        onActivated: nowPlaying.hide()
    }
    Shortcut {
        sequences: [StandardKey.Find, "/"]
        onActivated: {
            nowPlaying.hide()
            searchField.forceActiveFocus()
        }
    }
    Shortcut {
        sequence: "Space"
        onActivated: App.player.togglePause()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        onActivated: App.playback.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        onActivated: App.playback.previous()
    }
    Shortcut {
        sequences: ["Ctrl+Up", "Volume Up"]
        onActivated: App.setVolumeSlider(App.settings.volume + App.settings.volumeStep)
    }
    Shortcut {
        sequences: ["Ctrl+Down", "Volume Down"]
        onActivated: App.setVolumeSlider(App.settings.volume - App.settings.volumeStep)
    }
    Shortcut {
        sequences: ["M", "Volume Mute"]
        onActivated: App.player.muted = !App.player.muted
    }
    Shortcut {
        sequences: ["Media Play", "Media Pause", "Toggle Media Play/Pause"]
        onActivated: App.player.togglePause()
    }
    Shortcut {
        sequence: "Media Next"
        onActivated: App.playback.next()
    }
    Shortcut {
        sequence: "Media Previous"
        onActivated: App.playback.previous()
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: {
            window.queueOpen = true
            sidePanel.tab = 1
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+M"
        onActivated: window.openMiniPlayer()
    }
    Shortcut {
        sequence: "Ctrl+P"
        onActivated: nowPlaying.open ? nowPlaying.hide() : nowPlaying.show(playerBar.artItem)
    }

    // Everything but popups and the Now Playing view; glass blurs this.
    Item {
        id: scene
        anchors.fill: parent

        Backdrop {
            anchors.fill: parent
            translucent: App.settings.transparentWindow
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            // Under the open Now Playing nothing may react: a press goes to the
            // pointer handlers of every item under it once one of them took a
            // passive grab, so a tap on a queue row also tapped the card behind
            // (it opened that page and closed Now Playing). Only once fully
            // covered, so the controls do not turn grey while it slides in.
            enabled: nowPlaying.progress < 1

            // back and forward, search, clock
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                Layout.leftMargin: 12
                Layout.rightMargin: 16
                spacing: 2

                ToolButton {
                    icon.source: "icons/back.svg"
                    enabled: stack.depth > 1 || nowPlaying.open
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back")
                    onClicked: window.back()
                }
                ToolButton {
                    icon.source: "icons/back.svg"
                    rotation: 180
                    enabled: window.forwardPages.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Forward")
                    onClicked: window.forward()
                }
                Item {
                    Layout.fillWidth: true
                }
                SearchField {
                    id: searchField
                    Layout.preferredWidth: 560
                    Layout.maximumWidth: 560
                    Layout.fillWidth: true
                    onSearchRequested: query => {
                        nowPlaying.hide()
                        window.search(query)
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    id: clock
                    visible: App.settings.clock
                    color: Style.textDim
                    font.pixelSize: 14
                    font.features: { "tnum": 1 }
                    // the system's format, or 24 or 12 hours; seconds if asked
                    readonly property string format: {
                        const seconds = App.settings.clockSeconds
                        if (App.settings.clockHours === "24")
                            return seconds ? "HH:mm:ss" : "HH:mm"
                        if (App.settings.clockHours === "12")
                            return seconds ? "h:mm:ss AP" : "h:mm AP"
                        const short = Qt.locale().timeFormat(Locale.ShortFormat)
                        return seconds ? short.replace("mm", "mm:ss") : short
                    }
                    text: Qt.formatTime(new Date(), format)

                    Timer {
                        interval: 1000
                        repeat: true
                        running: clock.visible
                        onTriggered: clock.text = Qt.formatTime(new Date(), clock.format)
                    }
                }
                // keeps the search centered against the two buttons on the left
                Item {
                    Layout.preferredWidth: clock.visible ? 0 : 80
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Sidebar {
                    Layout.preferredWidth: App.settings.compactSidebar ? 72 : 232
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    compact: App.settings.compactSidebar
                    section: window.section
                    onSectionRequested: (name, browseId) => {
                        nowPlaying.hide()
                        window.goSection(name, browseId)
                    }

                    Behavior on Layout.preferredWidth {
                        NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
                    }
                }

                // the pages lie on a slightly lighter, rounded sheet
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: window.queueOpen ? 0 : 12
                    radius: Style.radiusLarge
                    color: Qt.alpha(Style.surface, 0.42)
                    border.color: Style.line
                    clip: true

                    StackView {
                        id: stack
                        anchors.fill: parent
                        initialItem: browseComponent

                        // forward: the new page slides in from the right
                        pushEnter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "x"; from: 48; to: 0; duration: Style.slow; easing.type: Easing.OutCubic }
                            }
                        }
                        pushExit: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
                                NumberAnimation { property: "x"; from: 0; to: -24; duration: Style.normal; easing.type: Easing.OutCubic }
                            }
                        }
                        popEnter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "x"; from: -48; to: 0; duration: Style.slow; easing.type: Easing.OutCubic }
                            }
                        }
                        popExit: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
                                NumberAnimation { property: "x"; from: 0; to: 24; duration: Style.normal; easing.type: Easing.OutCubic }
                            }
                        }
                        // another section: a short rise
                        replaceEnter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "y"; from: 16; to: 0; duration: Style.slow; easing.type: Easing.OutCubic }
                            }
                        }
                        replaceExit: Transition {
                            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
                        }

                        // mouse back and forward buttons
                        TapHandler {
                            acceptedButtons: Qt.BackButton | Qt.ForwardButton
                            onTapped: (point, button) => button === Qt.ForwardButton ? window.forward() : window.back()
                        }
                    }
                }

                QueuePanel {
                    id: sidePanel
                    Layout.preferredWidth: window.queueOpen ? 380 : 0
                    Layout.fillHeight: true
                    visible: Layout.preferredWidth > 1
                    clip: true

                    Behavior on Layout.preferredWidth {
                        NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
                    }
                }
            }

            PlayerBar {
                id: playerBar
                Layout.fillWidth: true
                queueOpen: window.queueOpen
                onQueueToggled: window.queueOpen = !window.queueOpen
                onNowPlayingRequested: nowPlaying.open ? nowPlaying.hide() : nowPlaying.show(playerBar.artItem)
                onNowPlayingShown: nowPlaying.show(playerBar.artItem)
                onMiniPlayerRequested: window.openMiniPlayer()
            }
        }
    }

    NowPlaying {
        id: nowPlaying
        anchors.fill: parent
    }

    MiniPlayer {
        id: miniPlayer
        visible: false
        onRestoreRequested: window.closeMiniPlayer()
        onClosing: window.closeMiniPlayer()
    }

    Component {
        id: browseComponent
        BrowseView {}
    }
    Component {
        id: downloadsComponent
        DownloadsView {}
    }
    Component {
        id: signInComponent
        SignInDialog {
            onClosed: destroy()
        }
    }
    Component {
        id: playlistPickerComponent
        PlaylistPicker {}
    }
    Component {
        id: settingsComponent
        SettingsDialog {}
    }
    Component {
        id: equalizerComponent
        EqualizerDialog {}
    }

    // the volume, for a moment after it changed by wheel or keys
    Item {
        id: volumeHud

        property real value: App.settings.volume

        function show() {
            opacity = 1
            hudTimer.restart()
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.18
        width: 220
        height: 64
        opacity: 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Style.normal }
        }

        Glass {
            anchors.fill: parent
            radius: height / 2
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 22
            spacing: 12

            Image {
                source: App.player.muted || volumeHud.value <= 0 ? "icons/volume-off.svg" : "icons/volume.svg"
                sourceSize: Qt.size(24, 24)
            }
            Rectangle {
                Layout.fillWidth: true
                height: 6
                radius: 3
                color: "#33ffffff"
                Rectangle {
                    width: parent.width * volumeHud.value / 100
                    height: parent.height
                    radius: 3
                    color: Style.accent
                    Behavior on width {
                        NumberAnimation { duration: Style.fast }
                    }
                }
            }
            Label {
                text: Math.round(volumeHud.value)
                color: Style.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                Layout.preferredWidth: 30
                horizontalAlignment: Text.AlignRight
            }
        }
        Timer {
            id: hudTimer
            interval: 1200
            onTriggered: volumeHud.opacity = 0
        }
    }

    // errors and short messages, shown for a while above the player bar
    Item {
        id: toast

        property string message
        property bool error: false

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: playerBar.height + 16
        width: Math.min(toastText.implicitWidth + 40, parent.width - 64)
        height: toastText.implicitHeight + 24
        opacity: 0
        visible: opacity > 0
        transform: Translate {
            y: toast.opacity < 1 ? 12 * (1 - toast.opacity) : 0
        }

        Behavior on opacity {
            NumberAnimation { duration: Style.normal; easing.type: Easing.OutCubic }
        }

        onMessageChanged: {
            opacity = message.length > 0 ? 1 : 0
            if (message.length > 0)
                toastTimer.restart()
        }

        Glass {
            anchors.fill: parent
            radius: height / 2
            tint: toast.error ? Qt.alpha("#5a2028", Style.effects ? 0.75 : 0.97) : Qt.alpha(Style.surfaceHigh, Style.effects ? 0.7 : 0.97)
        }

        Label {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.parent.width - 104)
            text: toast.message
            color: toast.error ? "#ffd9dc" : Style.text
            wrapMode: Text.Wrap
        }

        Timer {
            id: toastTimer
            interval: 6000
            onTriggered: toast.opacity = 0
        }
    }
}
