#pragma once

// Inactivity auto-off. When a scene is active (non-empty and not "Off") and no
// user action arrives for the configured window, the blaster powers off the AV
// gear (Sharp TV, Marantz amp, Apple TV) and sets its scene to "Off".
//
// The window length is runtime-configurable (the remote's settings screen reads
// and writes it via GET/POST /inactivity) and persisted in EEPROM so it survives
// reboots. Default is 60 minutes.
//
// During the final COUNTDOWN_MS (60 s) of the window the OLED shows a large
// seconds-left countdown, flashing in reverse video on alternate seconds. Any
// user action during the countdown resets the timer and cancels the warning.
//
// "User action" means a user-driven POST (see inactivity_note_activity callers);
// the remote's background GET polling must NOT reset the timer.

// Load the persisted timeout from EEPROM. Call once from setup() before the
// HTTP server starts (it can serve GET/POST /inactivity).
void
inactivity_begin();

void
inactivity_note_activity(); // call from the user-action POST handlers
void
inactivity_loop(); // call from loop()

// Accepted bounds for the configurable window, in minutes. The remote offers a
// discrete menu (15..180) within this range; the setter clamps to be safe.
constexpr unsigned int INACTIVITY_MIN_MINUTES = 1;
constexpr unsigned int INACTIVITY_MAX_MINUTES = 1440; // 24 h

// Current auto-off window, in minutes. Getter is cheap; setter clamps to
// [INACTIVITY_MIN_MINUTES, INACTIVITY_MAX_MINUTES], persists to EEPROM (only on
// an actual change), and resets the running timer so the new window starts now.
unsigned int
inactivity_get_timeout_minutes();
void
inactivity_set_timeout_minutes(unsigned int minutes);

// Snapshot of the running timer, for the on-screen "time remaining" indicator.
// Returns false (and leaves the outputs untouched) when no scene is active, so
// the Off screen draws no indicator. Otherwise fills *remaining_ms with the time
// left before auto-off and *total_ms with the full window. Either pointer may be
// null.
bool
inactivity_get_status(unsigned long* remaining_ms, unsigned long* total_ms);
