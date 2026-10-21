import QtQuick
import Quince

// Mouse wheel steps glide instead of jumping. Each notch moves a target and
// every frame the view eases towards it, so quick turns run together into
// one smooth motion instead of restarting an animation per notch; a fast
// spin covers more ground per notch. Touchpads keep their own pixel
// scrolling: they send scroll phases, a wheel does not. The device type
// alone does not tell (on Wayland a wheel may come as another device, with
// a small pixel delta the Flickable would crawl by). Put it inside the
// Flickable it scrolls (it then lives on the Flickable's contentItem), or
// set flickable. QT_LOGGING_RULES=quince.scroll.debug=true logs the events.
WheelHandler {
    id: handler

    property Flickable flickable: parent ? parent.parent : null
    // pixels per wheel notch at a slow turn
    property real step: 110
    // a fast spin covers up to this many steps per notch
    property real maxBoost: 3
    // seconds the glide takes to cover about two thirds of the way
    property real smoothness: 0.08

    // where the glide is heading
    property real goal: 0
    property real boost: 1
    property double lastNotch: 0

    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    orientation: Qt.Vertical

    property LoggingCategory log: LoggingCategory {
        name: "quince.scroll"
        defaultLogLevel: LoggingCategory.Warning
    }

    function bounds() {
        const f = flickable
        const top = f.originY - f.topMargin
        return {top: top, bottom: Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)}
    }

    onWheel: event => {
        const f = handler.flickable
        console.debug(handler.log, "wheel", event.device ? event.device.type : "?", "angle", event.angleDelta.y,
                      "pixel", event.pixelDelta.y, "phase", event.phase, "inverted", event.inverted)
        // a touchpad (phases) or a sideways push: the Flickable's own scrolling
        if (!f || event.phase !== Qt.NoScrollPhase || event.angleDelta.y === 0) {
            event.accepted = false
            return
        }
        const now = Date.now()
        // notches closer than this build up speed, a pause lets it fall back
        const gap = now - handler.lastNotch
        handler.boost = gap < 90 ? Math.min(handler.maxBoost, handler.boost + 0.35) : 1
        handler.lastNotch = now

        const b = handler.bounds()
        // a direction change starts from where the view is
        const from = glide.running && Math.sign(handler.goal - f.contentY) === -Math.sign(event.angleDelta.y)
                   ? handler.goal : f.contentY
        // up to a screen past the end: rows loaded on the way extend the glide
        handler.goal = Math.max(b.top, Math.min(b.bottom + f.height,
                                                  from - event.angleDelta.y / 120 * handler.step * handler.boost))
        if (f.flicking)
            f.cancelFlick()
        glide.running = true
        event.accepted = true
    }

    property FrameAnimation glide: FrameAnimation {
        onTriggered: {
            const f = handler.flickable
            // a drag or a touchpad flick takes over
            if (!f || f.dragging || f.flicking) {
                stop()
                return
            }
            const b = handler.bounds()
            const end = Math.max(b.top, Math.min(b.bottom, handler.goal))
            const left = end - f.contentY
            if (Math.abs(left) < 0.5) {
                f.contentY = end
                // the end stands; a goal beyond it is dropped
                handler.goal = end
                stop()
                return
            }
            // frame-rate independent easing towards the goal
            f.contentY += left * (1 - Math.exp(-frameTime / handler.smoothness))
        }
    }
}
