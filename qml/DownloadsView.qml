import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// The downloads of this session, with their progress.
Item {
    id: view

    readonly property Downloader downloads: App.downloads

    function stateText(state, error) {
        switch (state) {
        case "waiting": return qsTr("Waiting")
        case "resolving": return qsTr("Starting")
        case "downloading": return qsTr("Downloading")
        case "writing": return qsTr("Writing the file")
        case "done": return qsTr("Done")
        case "failed": return qsTr("Failed: %1").arg(error)
        }
        return ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.margin
        anchors.rightMargin: Style.margin
        anchors.topMargin: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Downloads")
                color: Style.text
                font.pixelSize: 28
                font.weight: Font.Bold
            }
            Button {
                text: qsTr("Open folder")
                flat: true
                onClicked: view.downloads.openFolder(-1)
            }
            Button {
                text: qsTr("Clear finished")
                flat: true
                enabled: list.count > 0
                onClicked: view.downloads.clearFinished()
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Into %1. Right-click a song, album or playlist and choose Download.").arg(view.downloads.folder)
            color: Style.textDim
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: view.downloads
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            SmoothScroll {}

            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal }
            }

            delegate: Item {
                id: row
                required property int index
                required property string title
                required property string artist
                required property string album
                required property var thumbnail
                required property string state
                required property real progress
                required property string error

                width: ListView.view.width
                height: 64

                Rectangle {
                    anchors.fill: parent
                    radius: Style.radius
                    color: hover.hovered ? Style.hover : "transparent"
                }
                HoverHandler {
                    id: hover
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 14

                    Art {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        source: row.thumbnail || ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            color: Style.text
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: [row.artist, row.album].filter(s => !!s).join(" • ")
                            color: Style.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        // progress while it downloads
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            radius: 2
                            color: "#22ffffff"
                            visible: row.state === "downloading" || row.state === "writing"
                            Rectangle {
                                width: parent.width * row.progress
                                height: parent.height
                                radius: 2
                                color: Style.accent
                                Behavior on width {
                                    NumberAnimation { duration: Style.normal }
                                }
                            }
                        }
                    }
                    Label {
                        Layout.maximumWidth: 260
                        text: view.stateText(row.state, row.error)
                        color: row.state === "failed" ? "#ff9a9f" : Style.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        text: qsTr("Retry")
                        visible: row.state === "failed"
                        onClicked: view.downloads.retry(row.index)
                    }
                    ToolButton {
                        icon.source: "icons/library.svg"
                        visible: row.state === "done"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open folder")
                        onClicked: view.downloads.openFolder(row.index)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: list.count === 0
            text: qsTr("Nothing downloaded yet.")
            color: Style.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
