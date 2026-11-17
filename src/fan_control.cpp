#include "fan_control.h"

#include "ir_sender.h"

#include <Arduino.h>

/*
  Code table copied verbatim from OMOTE-Firmware
  src/devices/misc/device_haikuFan/device_haikuFan.cpp, which documents the
  derivation in full: extended NEC, 32 bits, no repeat, logical address 0xF0BA
  with an 8 bit command, bit-reversed into the word IRremoteESP8266's sendNEC()
  transmits. nbits/repeat are left to ir_sender_send's protocol defaults
  (kNECBits / kNoRepeat), so we pass 0/-1 as inactivity.cpp does.

  If a button does nothing, recapture with the OMOTE IR receiver GUI and fix
  both copies of the table.
*/

// Protocol number as understood by ir_sender_send (decode_type_t / the OMOTE
// IR_PROTOCOL_* enum, which share numbering). NEC = 3.
static constexpr int PROTO_NEC = 3;

static const char HAIKU_FAN_TOGGLE[]   = "0x5D0FC03F";
static const char HAIKU_FAN_UP[]       = "0x5D0F0AF5";
static const char HAIKU_FAN_DOWN[]     = "0x5D0F827D";
static const char HAIKU_LIGHT_TOGGLE[] = "0x5D0FC23D";
static const char HAIKU_LIGHT_UP[]     = "0x5D0F42BD";
static const char HAIKU_LIGHT_DOWN[]   = "0x5D0F8A75";

/*
  Turning the light *off* on purpose.

  The Haiku protocol has no discrete on/off for the light — only a toggle — so
  "off" can't be expressed as a single code: firing the toggle blind is a coin
  flip that turns the light back on half the time. It can be expressed as a
  sequence, though, because LIGHT + is absolute in the one way that matters: it
  always leaves the light ON, whether it was on or off beforehand. That
  manufactures a known state to toggle out of.

      light_off:  +  (light is now definitely on)
                  -  (undo the brightness step)
                  toggle  (a known-on light toggles to off)

      light_on:   +  (light is now definitely on)
                  -  (undo the brightness step)

  Both land in the requested state regardless of what the light was doing
  before, which is the whole point: Home Assistant can call light_off twice in a
  row and the light is off both times.

  One artifact: if the light was already at maximum brightness the + is clamped
  and the - is not, so it ends one step dimmer. It doesn't compound — the next
  cycle's + wins that step back before the - takes it again — so the light
  settles one step below max and stays there.
*/

// Gap between the codes of a sequence. Long enough that the fan reads them as
// separate button presses rather than one held button (NEC repeat frames arrive
// every ~108 ms), short enough that the longest sequence still returns well
// inside any HTTP client's timeout.
static constexpr unsigned long FAN_STEP_GAP_MS = 200;

static FanLightState s_light = FAN_LIGHT_STATE_UNKNOWN;

struct ActionName {
    const char* name;
    FanAction   action;
};

// First match wins on lookup; the aliases at the bottom are why the canonical
// names come first. fan_control_action_names() below must stay in sync.
static const ActionName kActions[] = {
    {"light_off",    FAN_LIGHT_OFF},
    {"light_on",     FAN_LIGHT_ON},
    {"light_toggle", FAN_LIGHT_TOGGLE},
    {"light_up",     FAN_LIGHT_UP},
    {"light_down",   FAN_LIGHT_DOWN},
    {"fan_toggle",   FAN_POWER_TOGGLE},
    {"fan_up",       FAN_SPEED_UP},
    {"fan_down",     FAN_SPEED_DOWN},
    {"off",          FAN_LIGHT_OFF},
    {"on",           FAN_LIGHT_ON},
    {"toggle",       FAN_LIGHT_TOGGLE},
};

FanAction
fan_control_parse_action(const String& name) {
    for (const ActionName& a : kActions) {
        if (name.equalsIgnoreCase(a.name)) return a.action;
    }
    return FAN_ACTION_INVALID;
}

const char*
fan_control_action_names() {
    return "light_off light_on light_toggle light_up light_down "
           "fan_toggle fan_up fan_down (aliases: off on toggle)";
}

const char*
fan_control_action_label(FanAction action) {
    switch (action) {
    case FAN_LIGHT_OFF:    return "Fan Light Off";
    case FAN_LIGHT_ON:     return "Fan Light On";
    case FAN_LIGHT_TOGGLE: return "Fan Light";
    case FAN_LIGHT_UP:     return "Fan Light +";
    case FAN_LIGHT_DOWN:   return "Fan Light -";
    case FAN_POWER_TOGGLE: return "Fan On/Off";
    case FAN_SPEED_UP:     return "Fan +";
    case FAN_SPEED_DOWN:   return "Fan -";
    default:               return "Fan";
    }
}

FanLightState
fan_control_light_state() {
    return s_light;
}

const char*
fan_control_light_state_name() {
    switch (s_light) {
    case FAN_LIGHT_STATE_ON:  return "on";
    case FAN_LIGHT_STATE_OFF: return "off";
    default:                  return "unknown";
    }
}

// Fires `count` codes back to back, spaced so the fan sees distinct presses.
// Bails on the first failure without touching s_light, since a half-sent
// sequence leaves the light somewhere we can't name.
static IrSendResult
run_sequence(const char* const* codes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (i) delay(FAN_STEP_GAP_MS);
        IrSendResult r = ir_sender_send(PROTO_NEC, codes[i], 0, -1);
        if (!r.ok) { return {false, r.error ? r.error : "IR send failed"}; }
    }
    return {true, nullptr};
}

IrSendResult
fan_control_run(FanAction action) {
    // The sequences that establish a known state; see the block comment above.
    static const char* const kLightOff[] = {HAIKU_LIGHT_UP, HAIKU_LIGHT_DOWN, HAIKU_LIGHT_TOGGLE};
    static const char* const kLightOn[]  = {HAIKU_LIGHT_UP, HAIKU_LIGHT_DOWN};

    const char* const* codes = nullptr;
    size_t             count = 1;
    const char*        single = nullptr;
    FanLightState      new_state = s_light; // unchanged unless the action implies one

    switch (action) {
    case FAN_LIGHT_OFF:
        codes     = kLightOff;
        count     = 3;
        new_state = FAN_LIGHT_STATE_OFF;
        break;

    case FAN_LIGHT_ON:
        codes     = kLightOn;
        count     = 2;
        new_state = FAN_LIGHT_STATE_ON;
        break;

    case FAN_LIGHT_TOGGLE:
        // The raw, single-code toggle. Non-deterministic by nature — kept for
        // when you want exactly one button press and nothing else.
        single = HAIKU_LIGHT_TOGGLE;
        if (s_light == FAN_LIGHT_STATE_ON) {
            new_state = FAN_LIGHT_STATE_OFF;
        } else if (s_light == FAN_LIGHT_STATE_OFF) {
            new_state = FAN_LIGHT_STATE_ON;
        }
        break;

    // A brightness step leaves the light on either way: if it was off, + brings
    // it up; if it was on, it stays on. Same reasoning the off sequence rests on.
    case FAN_LIGHT_UP:
        single    = HAIKU_LIGHT_UP;
        new_state = FAN_LIGHT_STATE_ON;
        break;
    case FAN_LIGHT_DOWN:
        single    = HAIKU_LIGHT_DOWN;
        new_state = FAN_LIGHT_STATE_ON;
        break;

    // The motor is a separate toggle with no state feedback and nothing here
    // depends on it, so we fire and forget.
    case FAN_POWER_TOGGLE: single = HAIKU_FAN_TOGGLE; break;
    case FAN_SPEED_UP:     single = HAIKU_FAN_UP; break;
    case FAN_SPEED_DOWN:   single = HAIKU_FAN_DOWN; break;

    default:
        return {false, "unknown action"};
    }

    IrSendResult r = codes ? run_sequence(codes, count) : run_sequence(&single, 1);
    if (!r.ok) return r;

    s_light = new_state;
    return {true, nullptr};
}
