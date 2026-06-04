#pragma once

// Inactivity auto-off. When a scene is active (non-empty and not "Off") and no
// user action arrives for INACTIVITY_TIMEOUT_MS (1 hour), the blaster powers off
// the AV gear (Sharp TV, Marantz amp, Apple TV) and sets its scene to "Off".
//
// During the final COUNTDOWN_MS (60 s) of the window the OLED shows a large
// seconds-left countdown, flashing in reverse video on alternate seconds. Any
// user action during the countdown resets the timer and cancels the warning.
//
// "User action" means a user-driven POST (see inactivity_note_activity callers);
// the remote's background GET polling must NOT reset the timer.

void
inactivity_note_activity(); // call from the user-action POST handlers
void
inactivity_loop(); // call from loop()
