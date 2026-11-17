#pragma once

// PIR motion sensor. Motion in the room counts as user activity for the
// inactivity auto-off, so the TV stays on while someone is watching even if
// nobody touches the remote. The auto-off now fires only after the configured
// window passes with no remote activity AND no motion.
//
// Compiles out entirely unless PIR_PIN is defined (D1 Mini: D0 / GPIO16). The
// ESP-01 build has no spare pin, so it keeps remote-only behaviour.
void
presence_init();
void
presence_loop(); // call from loop(), before inactivity_loop()

// True while motion has been seen within the last PRESENCE_ICON_HOLD_MS. Drives
// the walking-person icon on the OLED. Always false without PIR_PIN.
bool
presence_recent();
