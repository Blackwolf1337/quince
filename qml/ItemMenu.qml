import QtQuick
import QtQuick.Controls
import Quince

// Right-click menu of a track, album or playlist. Destroys itself when closed.
StyledMenu {
    id: menu

    required property var item
    // set when the item is a row of the queue
    property int queueIndex: -1

    readonly property bool queueable: !!item.videoId || !!item.playPlaylistId
    readonly property var firstArtist: (item.artists || []).find(a => !!a.id)

    onClosed: destroy()

    MenuItem {
        text: qsTr("Play next")
        enabled: menu.queueable
        onTriggered: App.playback.playNext(menu.item)
    }
    MenuItem {
        text: qsTr("Add to queue")
        enabled: menu.queueable
        onTriggered: App.playback.addToQueue(menu.item)
    }
    MenuItem {
        text: qsTr("Start radio")
        enabled: !!menu.item.videoId || !!menu.item.radioPlaylistId
        onTriggered: App.playback.startRadio(menu.item)
    }
    MenuItem {
        text: qsTr("Add to playlist")
        enabled: App.account.signedIn && !!menu.item.videoId
        onTriggered: App.requestDialog("addToPlaylist", {item: menu.item})
    }
    MenuItem {
        text: qsTr("Remove from queue")
        enabled: menu.queueIndex >= 0 && menu.queueIndex !== App.playback.queue.current
        height: menu.queueIndex >= 0 ? implicitHeight : 0
        visible: menu.queueIndex >= 0
        onTriggered: App.playback.queue.remove(menu.queueIndex)
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("Go to album")
        enabled: !!menu.item.album && !!menu.item.album.id
        onTriggered: App.openBrowse(menu.item.album.id)
    }
    MenuItem {
        text: menu.firstArtist ? qsTr("Go to %1").arg(menu.firstArtist.name) : qsTr("Go to artist")
        enabled: !!menu.firstArtist
        onTriggered: App.openBrowse(menu.firstArtist.id)
    }
}
