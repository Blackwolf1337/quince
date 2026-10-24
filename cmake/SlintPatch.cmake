# Run in Slint's source dir by FetchContent's PATCH_COMMAND (QUINCE_SLINT).
# A monitor that reports 0 Hz (Wine on Xvfb) makes the winit frame throttle
# divide by zero.
set(file internal/backends/winit/frame_throttle.rs)
file(READ ${file} text)
string(REPLACE ".and_then(|monitor| monitor.refresh_rate_millihertz())"
               ".and_then(|monitor| monitor.refresh_rate_millihertz()).filter(|r| *r > 0)"
               patched "${text}")
if(patched STREQUAL text AND NOT text MATCHES "filter\\(\\|r\\| \\*r > 0\\)")
    message(FATAL_ERROR "SlintPatch.cmake: frame_throttle.rs changed, patch no longer applies")
endif()
file(WRITE ${file} "${patched}")
