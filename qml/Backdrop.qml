import QtQuick
import Quince

// The window background: dark, with slow drifting light in the album color
// behind the header and the player bar.
Rectangle {
    id: backdrop

    // see-through for compositor blur (Settings: transparent window)
    property bool translucent: false

    color: translucent ? Qt.alpha(Style.background, 0.8) : Style.background
    clip: true

    Glow {
        id: top
        width: Math.max(backdrop.width * 0.75, 700)
        height: width * 0.55
        x: -width * 0.25
        y: -height * 0.55
        strength: 0.75

        SequentialAnimation on x {
            loops: Animation.Infinite
            NumberAnimation { to: -top.width * 0.1; duration: 21000; easing.type: Easing.InOutSine }
            NumberAnimation { to: -top.width * 0.3; duration: 21000; easing.type: Easing.InOutSine }
        }
    }
    Glow {
        width: Math.max(backdrop.width * 0.45, 480)
        height: width * 0.6
        x: backdrop.width - width * 0.6
        y: -height * 0.5
        strength: 0.4
    }
    Glow {
        id: bottom
        width: Math.max(backdrop.width * 0.9, 800)
        height: 360
        x: (backdrop.width - width) / 2
        y: backdrop.height - height * 0.42
        strength: 0.55

        SequentialAnimation on strength {
            loops: Animation.Infinite
            NumberAnimation { to: 0.7; duration: 9000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.5; duration: 9000; easing.type: Easing.InOutSine }
        }
    }
}
