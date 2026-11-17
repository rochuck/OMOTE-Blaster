#pragma once

#include "ir_sender.h"

#include <Arduino.h>

// Big Ass Fans "Haiku L" ceiling fan + integrated LED light, driven off the
// blaster's own IR LED. Unlike every other code path here, these codes live on
// the blaster rather than on the remote: the point is that Home Assistant (or a
// browser, or curl) can turn the light off without the OMOTE being awake or
// involved at all. See fan_control.cpp for the code table, its provenance, and
// how a protocol with no discrete "off" is still made to turn the light off
// deterministically.

enum FanAction {
    FAN_LIGHT_OFF,
    FAN_LIGHT_ON,
    FAN_LIGHT_TOGGLE,
    FAN_LIGHT_UP,
    FAN_LIGHT_DOWN,
    FAN_POWER_TOGGLE,
    FAN_SPEED_UP,
    FAN_SPEED_DOWN,
    FAN_ACTION_INVALID,
};

// Maps "light_off", "fan_up", ... to a FanAction, case-insensitively. Bare
// "off"/"on"/"toggle" are accepted as light aliases, since the light is what
// this endpoint mostly exists for. FAN_ACTION_INVALID for anything else.
FanAction
fan_control_parse_action(const String& name);

// Space-separated list of the accepted names, for 400-response bodies.
const char*
fan_control_action_names();

// What the light is doing. FAN_LIGHT_OFF and FAN_LIGHT_ON put it in a known
// state, so this is exact after either of them; it only degrades if someone
// works the fan's own remote, which reports nothing back to us. UNKNOWN until
// the blaster has driven the light itself since boot.
enum FanLightState {
    FAN_LIGHT_STATE_UNKNOWN,
    FAN_LIGHT_STATE_ON,
    FAN_LIGHT_STATE_OFF,
};

// Fires the IR for `action`. Some actions are multi-code sequences that block
// for a few hundred ms; see FAN_STEP_GAP_MS in fan_control.cpp. Reuses
// ir_sender's result type — {ok, error} is all there is to report.
IrSendResult
fan_control_run(FanAction action);

FanLightState
fan_control_light_state();
const char*
fan_control_light_state_name(); // "on" / "off" / "unknown"

// Display label for an action ("Fan Light Off"), for the OLED.
const char*
fan_control_action_label(FanAction action);
