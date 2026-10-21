import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Quince

// Mood chips on the home page, filters on the search and library pages, and
// the library's sort menu on the right.
Item {
    id: chipsRow

    required property var row

    signal chipClicked(var chip)
    signal sortChosen(string token)

    readonly property var sort: row.sort || ({})

    implicitHeight: Math.max(flow.implicitHeight, sortBox.visible ? sortButton.implicitHeight : 0) + 16

    Flow {
        id: flow
        x: Style.margin
        y: 8
        width: parent.width - 2 * Style.margin - (sortBox.visible ? sortBox.width + 12 : 0)
        spacing: 8

        Repeater {
            model: chipsRow.row.chips || []
            Button {
                required property var modelData
                // an icon chip (clear the filter) looks like a button, not a chosen chip
                readonly property bool iconOnly: !modelData.title && modelData.icon === "close"
                text: modelData.title || ""
                icon.source: iconOnly ? "icons/close.svg" : ""
                display: iconOnly ? AbstractButton.IconOnly : AbstractButton.TextOnly
                ToolTip.visible: iconOnly && hovered
                ToolTip.text: modelData.label || ""
                readonly property bool chosen: !iconOnly && !!modelData.selected
                // the chosen filter in the album color, so it reads at a glance
                Material.foreground: chosen ? Style.background : Style.text
                background: Rectangle {
                    radius: height / 2
                    color: parent.chosen ? Style.accent : parent.pressed ? Style.pressed : parent.hovered ? Style.hover
                                                                                                         : "transparent"
                    border.width: parent.chosen || parent.iconOnly ? 0 : 1
                    border.color: Style.line
                    Behavior on color {
                        ColorAnimation { duration: Style.fast }
                    }
                }
                leftPadding: iconOnly ? 8 : 16
                rightPadding: iconOnly ? 8 : 16
                topInset: 0
                bottomInset: 0
                implicitHeight: 34
                // Material pads 14 above and below; at this height the icon would shrink to fit
                topPadding: 4
                bottomPadding: 4
                onClicked: chipsRow.chipClicked(modelData)
            }
        }
    }

    // the box keeps the button on the right; mirroring inside only puts the arrow after the text
    Item {
        id: sortBox
        anchors.right: parent.right
        anchors.rightMargin: Style.margin
        y: 8
        width: sortButton.implicitWidth
        height: sortButton.implicitHeight
        visible: !!chipsRow.sort.options

        Button {
            id: sortButton
            text: chipsRow.sort.title || ""
            icon.source: "icons/chevron-down.svg"
            icon.width: 20
            icon.height: 20
            flat: true
            LayoutMirroring.enabled: true
            Material.roundedScale: Material.SmallScale
            topInset: 0
            bottomInset: 0
            implicitHeight: 34
            topPadding: 4
            bottomPadding: 4
            onClicked: sortMenu.popup(this, 0, height)

            StyledMenu {
                id: sortMenu
                Repeater {
                    model: chipsRow.sort.options || []
                    MenuItem {
                        required property var modelData
                        text: modelData.title
                        checkable: true
                        checked: !!modelData.selected
                        onTriggered: chipsRow.sortChosen(modelData.token)
                    }
                }
            }
        }
    }
}
