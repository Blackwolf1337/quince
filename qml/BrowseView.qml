import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQml.Models
import Quince

// Any YouTube Music page: home, explore, album, artist, playlist or a search.
Item {
    id: view

    property alias browseId: page.browseId
    property alias params: page.params
    property alias query: page.query
    readonly property string title: page.header.title || (page.query ? page.query : "")

    function applyChip(chip) {
        if (chip.selected) {
            page.params = ""
            // library filters lead back to the library itself
            if (chip.deselectBrowseId)
                page.browseId = chip.deselectBrowseId
            return
        }
        page.params = chip.params || ""
        if (chip.browseId)
            page.browseId = chip.browseId
    }

    BrowsePage {
        id: page
    }

    ListView {
        id: list

        anchors.fill: parent
        model: page
        cacheBuffer: 1600
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: 24

        ScrollBar.vertical: ScrollBar {}

        function loadMoreIfNeeded() {
            if (page.canLoadMore && contentHeight - contentY - height < 800)
                page.loadMore()
        }

        onContentYChanged: loadMoreIfNeeded()
        onContentHeightChanged: loadMoreIfNeeded()

        SmoothScroll {}

        delegate: DelegateChooser {
            role: "rowType"

            DelegateChoice {
                roleValue: "header"
                HeaderRow {
                    width: ListView.view.width
                    onPlayRequested: App.playback.play(row)
                    onRateAllRequested: status => App.account.rateAll(page.trackIds(), status)
                }
            }
            DelegateChoice {
                roleValue: "chips"
                ChipsRow {
                    width: ListView.view.width
                    onChipClicked: chip => view.applyChip(chip)
                    onSortChosen: token => page.sortBy(token)
                }
            }
            DelegateChoice {
                roleValue: "title"
                TitleRow {
                    width: ListView.view.width
                }
            }
            DelegateChoice {
                roleValue: "item"
                ItemRow {
                    required property var row
                    x: Style.margin - 10
                    width: ListView.view.width - 2 * Style.margin + 20
                    item: row.item
                    showIndex: !!row.albumTracks
                    onActivated: App.open(row.item)
                }
            }
            DelegateChoice {
                roleValue: "carousel"
                CarouselRow {
                    width: ListView.view.width
                    onItemActivated: item => App.open(item)
                }
            }
            DelegateChoice {
                roleValue: "grid"
                GridRow {
                    width: ListView.view.width
                }
            }
            DelegateChoice {
                roleValue: "card"
                CardRow {
                    width: ListView.view.width
                }
            }
            DelegateChoice {
                roleValue: "text"
                TextRow {
                    width: ListView.view.width
                }
            }
        }

        footer: Item {
            width: ListView.view.width
            height: page.loading || page.error ? 96 : 24

            BusyIndicator {
                anchors.centerIn: parent
                running: page.loading && list.count > 0
                visible: running
            }
            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !page.loading && page.error.length > 0

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: page.error
                    color: Material.color(Material.Red, Material.Shade300)
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Try again")
                    flat: true
                    onClicked: page.canLoadMore ? page.loadMore() : page.reload()
                }
            }
        }
    }

    // the shape of a page while the first part of it loads
    Skeleton {
        anchors.fill: parent
        opacity: page.loading && list.count === 0 ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: Style.normal }
        }
    }
}
