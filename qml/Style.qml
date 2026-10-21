pragma Singleton

import QtQuick
import Quince

// colors with alpha are #AARRGGBB
QtObject {
    // from the cover of the playing track (album colors), or the plain defaults
    readonly property color accent: App.theme.accent
    readonly property color background: App.theme.background
    readonly property color surface: App.theme.surface
    readonly property color surfaceHigh: App.theme.surfaceHigh
    readonly property color hover: "#14ffffff"
    readonly property color placeholder: "#2c2c32"
    readonly property color text: "#f2f2f4"
    readonly property color textDim: "#a2a2ac"

    // left and right page margin
    readonly property int margin: 28
    readonly property int cardWidth: 172
    readonly property int wideCardWidth: 300
}
