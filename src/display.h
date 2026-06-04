#pragma once

#include <Arduino.h>

// SSD1306 128x32 OLED on the reserved I2C bus (D2/GPIO4 SDA, D1/GPIO5 SCL).
//
// The whole module compiles out unless ENABLE_DISPLAY is defined, so the
// pin-starved ESP-01 build is unaffected. All functions are safe no-ops when
// the panel is absent or the module is compiled out.
void
display_init(); // bring up the panel, show the boot splash
void
display_loop();

// Post-boot status screens. display_show_connected() prints the LAN IP once
// associated; display_show_ap_mode() prints captive-portal join instructions
// while WiFiManager is serving its config AP.
void
display_show_connected(const String& ip);
void
display_show_ap_mode(const String& ssid, const String& portal_ip);

// Firmware-upload screens, driven by the OTA handler. display_show_ota_progress
// draws a labelled progress bar; it self-throttles, so calling it on every
// upload chunk is cheap (it only repaints when the bar advances a pixel).
// display_show_ota_result paints the final OK/FAIL state.
void
display_show_ota_progress(size_t written, size_t total);
void
display_show_ota_result(bool ok);

// Inactivity auto-off countdown. display_show_countdown draws a large centered
// seconds-left number; invert=true renders it in reverse video (full-screen
// flash). It wakes the panel and suppresses the normal display_loop refresh/
// sleep state machine until display_clear_countdown() repaints the live scene.
void
display_show_countdown(int seconds, bool invert);
void
display_clear_countdown();
