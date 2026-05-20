#pragma once

#include <Arduino.h>

// Runtime state the remote pushes to us for display/diagnostics. The blaster is
// otherwise stateless; this is the single source the (future) display reads.
//
// - scene:     human-readable active scene on the remote ("Apple TV", "Off", "").
// - lastCommand: human-readable label of the last IR command ("ATV PLAY"), or
//                empty if the remote sent no name with it.
// - lastCommandMillis: millis() when the last /send arrived (0 = none yet).

void
blaster_state_set_scene(const String& scene);
const String&
blaster_state_get_scene();

// Bumps each time the scene actually changes value. The display watches this as
// a "repaint the scene" edge, independent of IR commands.
uint32_t
blaster_state_scene_version();

void
blaster_state_set_last_command(const String& name);
const String&
blaster_state_get_last_command();

// millis() of the last /send, or 0 if none. Use blaster_state_last_command_age_s
// for a display-friendly "seconds ago" (returns 0 when none yet).
unsigned long
blaster_state_last_command_millis();
uint32_t
blaster_state_last_command_age_s();
