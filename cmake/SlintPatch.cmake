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
# not 60; quick notches already add up in Slint's Flickable
slint_patch(internal/backends/winit/winitwindowadapter.rs
    "winit::event::MouseScrollDelta::LineDelta(lx, ly) => (lx * 60., ly * 60.),"
    "winit::event::MouseScrollDelta::LineDelta(lx, ly) => (lx * 110., ly * 110.),"
    "the wheel step")
