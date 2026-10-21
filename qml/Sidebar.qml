import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Sections, the account's library and playlists, and who is signed in.
ColumnLayout {
    id: sidebar

    // name of the open section: home, explore, library, liked, history, or a playlist browse id
    property string section: "home"

    signal sectionRequested(string name, string browseId)

    spacing: 2

    component SideItem: ItemDelegate {
        property string name
        property string browseId
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        highlighted: sidebar.section === name
        onClicked: sidebar.sectionRequested(name, browseId)
    }

    SideItem {
        Layout.topMargin: 12
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

    Rectangle {
        Layout.fillWidth: true
        Layout.margins: 16
        height: 1
        color: "#1affffff"
        visible: App.account.signedIn
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: App.account.playlists
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            required property var modelData
            width: ListView.view.width - 20
            x: 10
            text: modelData.title
            font.pixelSize: 13
            highlighted: sidebar.section === modelData.browseId
            onClicked: sidebar.sectionRequested(modelData.browseId, modelData.browseId)
        }
    }

    ItemDelegate {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        text: qsTr("Settings")
        icon.source: "icons/settings.svg"
        onClicked: App.requestDialog("settings")
    }

    // who is signed in, or the way to sign in
    ItemDelegate {
        Layout.fillWidth: true
        Layout.margins: 10
        text: App.account.signedIn ? (App.account.name || qsTr("Signed in")) : qsTr("Sign in")
        icon.source: App.account.signedIn ? "" : "icons/account.svg"
        leftPadding: App.account.signedIn ? 52 : padding

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
