import QtQuick
import QtQuick.Controls
import Quince

// Menu with its own background; Material draws its one with an effect the
// software renderer does not have.
Menu {
    background: Rectangle {
        implicitWidth: 240
        color: Style.surfaceHigh
        radius: 8
        border.color: "#22ffffff"
    }
}
