import QtQuick
import QtQuick.Shapes
import Quince

// A soft round light that fades to nothing at its edge.
Shape {
    id: glow

    property color color: Style.glow
    property real strength: 0.5

    width: 600
    height: 600

    ShapePath {
        strokeWidth: -1
        strokeColor: "transparent"
        fillGradient: RadialGradient {
            centerX: glow.width / 2
            centerY: glow.height / 2
            centerRadius: Math.min(glow.width, glow.height) / 2
            focalX: centerX
            focalY: centerY
            // a smooth falloff (1 - r)^2.5; few stops show as rings
            GradientStop { position: 0.0; color: Qt.alpha(glow.color, glow.strength) }
            GradientStop { position: 0.15; color: Qt.alpha(glow.color, glow.strength * 0.666) }
            GradientStop { position: 0.3; color: Qt.alpha(glow.color, glow.strength * 0.41) }
            GradientStop { position: 0.45; color: Qt.alpha(glow.color, glow.strength * 0.226) }
            GradientStop { position: 0.6; color: Qt.alpha(glow.color, glow.strength * 0.101) }
            GradientStop { position: 0.75; color: Qt.alpha(glow.color, glow.strength * 0.031) }
            GradientStop { position: 0.9; color: Qt.alpha(glow.color, glow.strength * 0.003) }
            GradientStop { position: 1.0; color: Qt.alpha(glow.color, 0) }
        }
        PathRectangle {
            width: glow.width
            height: glow.height
        }
    }
}
