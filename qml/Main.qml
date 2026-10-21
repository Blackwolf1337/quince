import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

ApplicationWindow {
    id: window

    property string section: "home"
    property bool queueOpen: false

    function goSection(name, browseId) {
        section = name
        stack.replace(null, browseComponent, {browseId: browseId || ""})
    }

    function showToast(text, isError) {
        toast.message = ""
        toast.error = isError
        toast.message = text
    }

    function search(query) {
        stack.push(browseComponent, {query: query})
    }

    function back() {
        if (stack.depth > 1)
            stack.pop()
    }

    width: 1280
    height: 800
    minimumWidth: 820
    minimumHeight: 520
    visible: true
    color: Style.background
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
    Material.background: Style.background

    Connections {
        target: App
        function onNavigateRequested(target) {
            stack.push(browseComponent, {browseId: target.browseId, params: target.params || ""})
        }
        function onDialogRequested(name, data) {
            if (name === "signin")
                signInComponent.createObject(window.contentItem).open()
            else if (name === "addToPlaylist")
                playlistPickerComponent.createObject(window.contentItem, {item: data.item}).open()
            else if (name === "settings")
                settingsComponent.createObject(window.contentItem).open()
        }
    }
    Connections {
        target: App.account
        // pages and the home cache belong to the account they were loaded for
        function onSignedInChanged() {
            window.goSection("home", "")
        }
        function onMessage(text) {
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

    Shortcut {
        sequences: [StandardKey.Back, "Alt+Left"]
        onActivated: window.back()
    }
    Shortcut {
        sequences: [StandardKey.Find, "/"]
        onActivated: searchField.forceActiveFocus()
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

    header: ToolBar {
        implicitHeight: 60

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12

            ToolButton {
                icon.source: "icons/back.svg"
                enabled: stack.depth > 1
                onClicked: window.back()
            }
            Item {
                Layout.fillWidth: true
            }
            SearchField {
                id: searchField
                Layout.preferredWidth: 560
                Layout.maximumWidth: 560
                Layout.fillWidth: true
                onSearchRequested: query => window.search(query)
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: 220
            Layout.fillWidth: false
            Layout.fillHeight: true
            section: window.section
            onSectionRequested: (name, browseId) => window.goSection(name, browseId)
        }

        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            initialItem: browseComponent

            pushEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
            }
            pushExit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 }
            }
            popEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
            }
            popExit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 }
            }
            replaceEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
            }
            replaceExit: Transition {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 }
            }

            // mouse back button
            TapHandler {
                acceptedButtons: Qt.BackButton
                onTapped: window.back()
            }
        }

        QueuePanel {
            id: sidePanel
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            visible: window.queueOpen
        }
    }

    footer: PlayerBar {
        queueOpen: window.queueOpen
        onQueueToggled: window.queueOpen = !window.queueOpen
    }

    Component {
        id: browseComponent
        BrowseView {}
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

    // errors and short messages, shown for a while above the player bar
    Rectangle {
        id: toast

        property string message
        property bool error: false

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(toastText.implicitWidth + 32, parent.width - 64)
        height: toastText.implicitHeight + 20
        radius: 8
        color: error ? "#3a2326" : Style.surfaceHigh
        visible: false

        onMessageChanged: {
            visible = message.length > 0
            if (visible)
                toastTimer.restart()
        }

        Label {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.parent.width - 96)
            text: toast.message
            color: toast.error ? "#ffd9dc" : Style.text
            wrapMode: Text.Wrap
        }

        Timer {
            id: toastTimer
            interval: 6000
            onTriggered: toast.visible = false
        }
    }
}
