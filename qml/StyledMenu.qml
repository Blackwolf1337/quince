import QtQuick
import QtQuick.Controls
import Quince

// Menu on glass; it opens with a short scale and fade.
Menu {
    id: menu

    topPadding: 6
    bottomPadding: 6

    background: Glass {
        implicitWidth: 240
        radius: 12
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.fast }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Style.normal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
    }
}
