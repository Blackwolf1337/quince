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
    readonly property color glow: App.theme.glow
    readonly property color hover: "#12ffffff"
    readonly property color pressed: "#1effffff"
    readonly property color line: "#14ffffff"
    readonly property color placeholder: "#24242a"
    readonly property color text: "#f4f4f6"
    readonly property color textDim: "#9c9ca8"

    readonly property string font: "Inter Variable"

    // left and right page margin
    readonly property int margin: 28
    readonly property int cardWidth: 172
    readonly property int wideCardWidth: 300
    readonly property int radius: 10
    readonly property int radiusLarge: 16

    // durations of the motion, in ms
    readonly property int fast: 140
    readonly property int normal: 260
    readonly property int slow: 420

    // What glass blurs: the window's scene without popups. Main sets it.
    property Item backdrop: null
    // shader effects (blur, masks); the software renderer has none
    property bool effects: true
    // the mini player is open (it then draws the video, not the main window)
    property bool miniPlayer: false
}
