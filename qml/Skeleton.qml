import QtQuick
import Quince

// Placeholder shapes while a page loads, with a light sweeping over them.
Item {
    id: skeleton

    component Bone: Rectangle {
        color: Style.placeholder
        radius: 6
    }

    clip: true

    Column {
        x: Style.margin
        y: 24
        width: skeleton.width - 2 * Style.margin
        spacing: 18

        Bone { width: 260; height: 28 }
        Row {
            spacing: 18
            Repeater {
                model: Math.max(1, Math.floor((skeleton.width - 2 * Style.margin) / (Style.cardWidth + 18)))
                Column {
                    spacing: 10
                    Bone { width: Style.cardWidth; height: Style.cardWidth; radius: Style.radius }
                    Bone { width: Style.cardWidth * 0.8; height: 14 }
                    Bone { width: Style.cardWidth * 0.5; height: 12 }
                }
            }
        }
        Bone { width: 200; height: 28 }
        Repeater {
            model: 5
            Row {
                spacing: 14
                Bone { width: 44; height: 44 }
                Column {
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Bone { width: 280; height: 14 }
                    Bone { width: 160; height: 12 }
                }
            }
        }
    }

    // the sweep
    Rectangle {
        id: sweep
        width: 260
        height: skeleton.height
        rotation: 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#00ffffff" }
            GradientStop { position: 0.5; color: "#0affffff" }
            GradientStop { position: 1.0; color: "#00ffffff" }
        }
        NumberAnimation on x {
            from: -sweep.width
            to: skeleton.width
            duration: 1300
            loops: Animation.Infinite
            running: skeleton.visible
        }
    }
}
