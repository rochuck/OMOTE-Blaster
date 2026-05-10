#pragma once

// Block (with the WiFi LED slow-blinking) until WiFi is configured and
// associated. If the portal button (PORTAL_BUTTON_PIN) is held LOW at
// boot, force the configuration portal even if creds are already saved.
void
wifi_setup_begin(const char* ap_name, const char* hostname);
