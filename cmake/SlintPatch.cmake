# Run in Slint's source dir by FetchContent's PATCH_COMMAND (QUINCE_SLINT).
# Each change is applied once; a source that no longer has the line stops the
# build, so a Slint upgrade cannot drop one silently.

# slint_patch(<file> <old text> <new text> <why>)
function(slint_patch file old new why)
    file(READ ${file} text)
    string(FIND "${text}" "${new}" done)
    if(NOT done EQUAL -1)
        return()
    endif()
    string(REPLACE "${old}" "${new}" patched "${text}")
    if(patched STREQUAL text)
        message(FATAL_ERROR "SlintPatch.cmake: ${file} changed, the patch for ${why} no longer applies")
    endif()
    file(WRITE ${file} "${patched}")
endfunction()

# a monitor that reports 0 Hz (Wine on Xvfb) makes the frame throttle divide by zero
slint_patch(internal/backends/winit/frame_throttle.rs
    ".and_then(|monitor| monitor.refresh_rate_millihertz())"
    ".and_then(|monitor| monitor.refresh_rate_millihertz()).filter(|r| *r > 0)"
    "the 0 Hz monitor")

# a mouse wheel notch scrolls 110 px as in the QML frontend (qml/SmoothScroll.qml),
# not 60, and notches closer than 90 ms build up to 3 steps each; quick notches
# already add up in Slint's Flickable
slint_patch(internal/backends/winit/winitwindowadapter.rs
    "winit::event::MouseScrollDelta::LineDelta(lx, ly) => (lx * 60., ly * 60.),"
[=[winit::event::MouseScrollDelta::LineDelta(lx, ly) => {
                        thread_local! {
                            static WHEEL: core::cell::Cell<(Option<std::time::Instant>, f32)> =
                                const { core::cell::Cell::new((None, 1.)) };
                        }
                        let boost = WHEEL.with(|wheel| {
                            let (last, boost) = wheel.get();
                            let now = std::time::Instant::now();
                            let fast = last.is_some_and(|l| now.duration_since(l).as_millis() < 90);
                            let boost = if fast { (boost + 0.35).min(3.) } else { 1. };
                            wheel.set((Some(now), boost));
                            boost
                        });
                        (lx * 110. * boost, ly * 110. * boost)
                    }]=]
    "the wheel step")

# a tooltip shows centered above its element, 24 px away, as Qt Quick's Material
# ToolTip (and the QML frontend), not below the pointer
slint_patch(internal/compiler/passes/lower_tooltips.rs
[=[    let tooltip_offset_expr = Expression::PropertyReference(tooltip_offset);
    let x_pointer = Expression::PropertyReference(pointer_x);
    let y_pointer = Expression::BinaryExpression {
        lhs: Box::new(Expression::PropertyReference(pointer_y)),
        rhs: Box::new(tooltip_offset_expr),
        op: '+',
        source_location: None,
    };]=]
[=[    let _ = (pointer_y, tooltip_offset);
    let area = pointer_x.element();
    let prop = |e: &ElementRc, name: &'static str| {
        Box::new(Expression::PropertyReference(NamedReference::new(e, SmolStr::new_static(name))))
    };
    let x_pointer = Expression::BinaryExpression {
        lhs: Box::new(Expression::BinaryExpression {
            lhs: prop(&area, WIDTH),
            rhs: prop(popup_window_rc, WIDTH),
            op: '-',
            source_location: None,
        }),
        rhs: Box::new(Expression::NumberLiteral(2., Unit::None)),
        op: '/',
        source_location: None,
    };
    let y_pointer = Expression::BinaryExpression {
        lhs: Box::new(Expression::NumberLiteral(-24., Unit::Px)),
        rhs: prop(popup_window_rc, HEIGHT),
        op: '-',
        source_location: None,
    };]=]
    "the tooltip position")

# a scroll bar hides until its view scrolls or the pointer is on it, and goes
# 2.45 s later, as Qt Quick's Material ScrollBar (QML frontend); not when the
# view jumps back to the top (a new page)
slint_patch(internal/compiler/widgets/fluent/scrollview.slint
[=[    visible: (self.policy == ScrollBarPolicy.always-on) || (self.policy == ScrollBarPolicy.as-needed && self.maximum > 0);
    clip: true;
]=]
[=[    visible: (self.policy == ScrollBarPolicy.always-on) || (self.policy == ScrollBarPolicy.as-needed && self.maximum > 0);
    clip: true;

    property <bool> lingering;
    opacity: touch-area.has-hover || touch-area.pressed || self.lingering ? 1 : 0;
    animate opacity { duration: 250ms; }
    changed value => {
        if root.value != 0 {
            root.lingering = true;
            linger.restart();
        }
    }
    // restart() only works on a timer that has run before
    linger := Timer {
        interval: 2450ms;
        running: root.lingering;
        triggered => {
            root.lingering = false;
        }
    }
]=]
    "the hiding scroll bar")
