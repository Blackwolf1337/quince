import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quince

// Title above a list shelf.
Item {
    id: titleRow

    required property var row

    implicitHeight: 64

    RowLayout {
        x: Style.margin
        width: parent.width - 2 * Style.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8

        Label {
            Layout.fillWidth: true
            text: titleRow.row.title || ""
            color: Style.text
            font.pixelSize: 22
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Button {
            text: qsTr("More")
            flat: true
            visible: !!titleRow.row.more
            onClicked: App.openBrowse(titleRow.row.more.browseId, titleRow.row.more.params)
        }
    }
}
