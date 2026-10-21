import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Quince

// The ways to bring a Google session into Quince.
Dialog {
    id: dialog

    title: qsTr("Sign in to YouTube Music")
    modal: true

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
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 48 : 520)
    standardButtons: Dialog.Close

    // the dialog closes once a session arrived
    Connections {
        target: App.account
        function onSignedInChanged() {
            if (App.account.signedIn)
                dialog.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Style.textDim
            text: qsTr("Signing in brings your library, likes, playlists and Premium quality. "
                       + "Quince keeps only the session cookies, readable by your user alone.")
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Sign in with Google")
            icon.source: "icons/account.svg"
            highlighted: true
            enabled: !App.account.busy
            onClicked: App.account.signIn()
        }

        Label {
            Layout.topMargin: 6
            visible: App.account.browsers.length > 0
            text: qsTr("Or take the session of a browser where you are signed in:")
            color: Style.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: App.account.browsers
                Button {
                    required property var modelData
                    text: modelData.name
                    flat: true
                    enabled: !App.account.busy
                    onClicked: App.account.importFromBrowser(modelData.id)
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Import a cookies.txt file")
            flat: true
            enabled: !App.account.busy
            onClicked: fileDialog.open()
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: App.account.busy
            visible: running
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("cookies.txt with a YouTube session")
        nameFilters: [qsTr("Cookie files (*.txt)"), qsTr("All files (*)")]
        onAccepted: App.account.importCookies(selectedFile)
    }
}
