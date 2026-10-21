import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Top of an album, playlist or artist page.
Item {
    id: header

    required property var row
    readonly property bool artist: row.style === "artist"
    readonly property bool titleOnly: row.style === "title"

    signal playRequested()
    // album-actions: rate every track of the page (LIKE, DISLIKE, INDIFFERENT)
    signal rateAllRequested(string status)

    implicitHeight: artist ? artistLayout.implicitHeight : titleOnly ? titleLayout.implicitHeight + 24
                                                                     : detailLayout.implicitHeight + 32

    // links of the artists in the strapline, as rich text
    function artistLinks(artists, fallback) {
        if (!artists || artists.length === 0)
            return fallback || ""
        return artists.map(a => a.id ? "<a href=\"" + a.id + "\">" + a.name + "</a>" : a.name).join(", ")
    }

    Label {
        id: titleLayout
        visible: header.titleOnly
        x: Style.margin
        y: 16
        text: header.row.title || ""
        color: Style.text
        font.pixelSize: 34
        font.weight: Font.Bold
    }

    RowLayout {
        id: detailLayout
        visible: !header.artist && !header.titleOnly
        x: Style.margin
        y: 16
        width: parent.width - 2 * Style.margin
        spacing: 28

        Art {
            Layout.preferredWidth: 232
            Layout.preferredHeight: 232
            Layout.alignment: Qt.AlignTop
            source: header.row.thumbnail || ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: header.row.title || ""
                color: Style.text
                font.pixelSize: 34
                font.weight: Font.Bold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: header.artistLinks(header.row.artists, header.row.strapline)
                visible: text.length > 0
                textFormat: Text.StyledText
                linkColor: Style.text
                color: Style.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                onLinkActivated: link => App.openBrowse(link)
                HoverHandler {
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
            Label {
                text: [header.row.subtitle, header.row.secondSubtitle].filter(s => !!s).join(" • ")
                color: Style.textDim
                font.pixelSize: 14
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: header.row.description || ""
                visible: text.length > 0
                color: Style.textDim
                font.pixelSize: 13
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.topMargin: 12
                spacing: 10

                PlayButtons {
                    target: header.row
                    onPlayRequested: header.playRequested()
                }
                RoundButton {
                    icon.source: "icons/download.svg"
                    flat: true
                    visible: !!header.row.browseId && (header.row.kind === "album" || header.row.kind === "playlist")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Download")
                    onClicked: App.downloads.download(header.row)
                }
                RoundButton {
                    icon.source: "icons/more.svg"
                    flat: true
                    visible: App.account.signedIn
                    enabled: !App.account.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("More")
                    onClicked: rateMenu.popup(this, 0, height)

                    StyledMenu {
                        id: rateMenu
                        MenuItem {
                            text: qsTr("Like all songs")
                            icon.source: "icons/like.svg"
                            onTriggered: header.rateAllRequested("LIKE")
                        }
                        MenuItem {
                            text: qsTr("Dislike all songs")
                            icon.source: "icons/dislike.svg"
                            onTriggered: header.rateAllRequested("DISLIKE")
                        }
                        MenuItem {
                            text: qsTr("Remove all ratings")
                            onTriggered: header.rateAllRequested("INDIFFERENT")
                        }
                    }
                }
            }
        }
    }

    Item {
        id: artistLayout
        visible: header.artist
        width: parent.width
        implicitHeight: banner.height + 8

        Art {
            id: banner
            width: parent.width
            height: Math.min(Math.round(width * 0.36), 380)
            source: header.row.thumbnail || ""
        }

        Rectangle {
            anchors.fill: banner
            gradient: Gradient {
                GradientStop { position: 0.35; color: Qt.rgba(Style.background.r, Style.background.g, Style.background.b, 0) }
                GradientStop { position: 1.0; color: Style.background }
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: banner.bottom
            anchors.leftMargin: Style.margin
            anchors.rightMargin: Style.margin
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: header.row.title || ""
                color: Style.text
                font.pixelSize: 46
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Label {
                text: header.row.subtitle || ""
                color: Style.textDim
                font.pixelSize: 14
            }
            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: 720
                text: header.row.description || ""
                visible: text.length > 0
                color: Style.textDim
                font.pixelSize: 13
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            PlayButtons {
                Layout.topMargin: 8
                target: header.row
                onPlayRequested: header.playRequested()
            }
        }
    }
}
