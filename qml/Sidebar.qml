import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Sections, the account's library and playlists, and who is signed in.
// Compact, it shows the icons only.
ColumnLayout {
    id: sidebar

    // name of the open section: home, explore, library, liked, history, or a playlist browse id
    property string section: "home"
    property bool compact: false

    signal sectionRequested(string name, string browseId)

    spacing: 2

    component SideButton: ItemDelegate {
        id: sideButton
        property bool selected: false
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        implicitHeight: 44
        display: sidebar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        font.pixelSize: 14
        font.weight: selected ? Font.DemiBold : Font.Normal
        icon.color: selected ? Style.accent : Style.text
        ToolTip.visible: sidebar.compact && hovered
        ToolTip.text: text
        ToolTip.delay: 300

        background: Rectangle {
            radius: Style.radius
            color: sideButton.selected ? Qt.alpha(Style.accent, 0.14)
                 : sideButton.pressed ? Style.pressed
                 : sideButton.hovered ? Style.hover : "transparent"
            Behavior on color {
                ColorAnimation { duration: Style.fast }
            }
            // the mark of the open section
            Rectangle {
                width: 3
                height: sideButton.selected ? 18 : 0
                radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: Style.accent
                Behavior on height {
                    NumberAnimation { duration: Style.normal; easing.type: Easing.OutBack }
                }
            }
        }
    }

    component SideItem: SideButton {
        property string name
        property string browseId
        selected: sidebar.section === name
        onClicked: sidebar.sectionRequested(name, browseId)
    }

    SideItem {
        Layout.topMargin: 4
        name: "home"
        text: qsTr("Home")
        icon.source: "icons/home.svg"
    }
    SideItem {
        name: "explore"
        browseId: "FEmusic_explore"
        text: qsTr("Explore")
        icon.source: "icons/explore.svg"
    }
    SideItem {
        name: "library"
        browseId: "FEmusic_library_landing"
        text: qsTr("Library")
        icon.source: "icons/library.svg"
        enabled: App.account.signedIn
    }
    SideItem {
        name: "liked"
        browseId: "VLLM"
        text: qsTr("Liked songs")
        icon.source: "icons/heart.svg"
        enabled: App.account.signedIn
    }
    SideItem {
        name: "history"
        browseId: "FEmusic_history"
        text: qsTr("History")
        icon.source: "icons/history.svg"
        enabled: App.account.signedIn
    }
    SideItem {
        name: "downloads"
        text: App.downloads.running > 0 ? qsTr("Downloads (%1)").arg(App.downloads.running) : qsTr("Downloads")
        icon.source: "icons/download.svg"
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.margins: 16
        height: 1
        color: Style.line
        visible: App.account.signedIn
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: App.account.playlists
        boundsBehavior: Flickable.StopAtBounds
        opacity: sidebar.compact ? 0 : 1
        visible: opacity > 0
        ScrollBar.vertical: ScrollBar {}

        Behavior on opacity {
            NumberAnimation { duration: Style.fast }
        }

        SmoothScroll {}

        delegate: SideButton {
            required property var modelData
            width: ListView.view.width - 20
            x: 10
            implicitHeight: 38
            text: modelData.title
            font.pixelSize: 13
            display: AbstractButton.TextOnly
            selected: sidebar.section === modelData.browseId
            onClicked: sidebar.sectionRequested(modelData.browseId, modelData.browseId)
        }
    }
    // keeps settings and the account at the bottom while compact
    Item {
        Layout.fillHeight: true
        visible: sidebar.compact
    }

    SideButton {
        text: qsTr("Settings")
        icon.source: "icons/settings.svg"
        onClicked: App.requestDialog("settings")
    }

    // who is signed in, or the way to sign in
    SideButton {
        id: accountButton
        Layout.bottomMargin: 10
        implicitHeight: 52
        text: App.account.signedIn ? (App.account.name || qsTr("Signed in")) : qsTr("Sign in")
        icon.source: App.account.signedIn ? "" : "icons/account.svg"
        leftPadding: App.account.signedIn ? (sidebar.compact ? 13 : 52) : padding
        display: sidebar.compact && App.account.signedIn ? AbstractButton.TextOnly : sidebar.compact ? AbstractButton.IconOnly
                                                                                                     : AbstractButton.TextBesideIcon
        contentItem.visible: !(sidebar.compact && App.account.signedIn)

        Art {
            visible: App.account.signedIn
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            round: true
            source: App.account.photo
        }

        onClicked: App.account.signedIn ? accountMenu.popup() : App.requestDialog("signin")

        StyledMenu {
            id: accountMenu
            MenuItem {
                text: App.account.handle || App.account.name
                enabled: false
            }
            MenuItem {
                text: qsTr("Sign out")
                onTriggered: App.account.signOut()
            }
        }
    }
}
