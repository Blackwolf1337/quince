import QtQuick
import Quince

// Behind modal dialogs: the window, blurred and darkened.
Glass {
    radius: 0
    border: false
    tint: Qt.alpha(Style.background, Style.effects ? 0.45 : 0.7)
}
