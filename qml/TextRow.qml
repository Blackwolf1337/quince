import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quince

// A text shelf, e.g. the artist's "About".
Item {
    id: textRow

    required property var row
    property bool expanded: false

    implicitHeight: column.implicitHeight + 32

    ColumnLayout {
        id: column
        x: Style.margin
        y: 24
        width: Math.min(parent.width - 2 * Style.margin, 900)
        spacing: 8

        Label {
            text: textRow.row.title || ""
            visible: text.length > 0
            color: Style.text
            font.pixelSize: 22
            font.weight: Font.Bold
        }
        Label {
            id: body
            Layout.fillWidth: true
            text: textRow.row.text || ""
            color: Style.textDim
            font.pixelSize: 14
            wrapMode: Text.Wrap
            maximumLineCount: textRow.expanded ? 1000 : 4
            elide: Text.ElideRight
        }
        Button {
            text: textRow.expanded ? qsTr("Less") : qsTr("More")
            flat: true
            visible: body.truncated || textRow.expanded
            onClicked: textRow.expanded = !textRow.expanded
        }
    }
}
