#include "presence.h"

#ifdef PIR_PIN

#include "inactivity.h"

#include <Arduino.h>

// The pin must hold HIGH this long before we believe it (rejects glitches).
static constexpr unsigned long DEBOUNCE_MS = 50;
// While motion persists, re-stamp the inactivity timer at most this often.
static constexpr unsigned long NOTE_INTERVAL_MS = 1000;
// How long the OLED walker icon lingers after the sensor goes quiet.
static constexpr unsigned long PRESENCE_ICON_HOLD_MS = 60UL * 1000UL;

static bool          s_motion         = false; // debounced sensor state
static unsigned long s_high_since_ms  = 0;     // 0 = pin currently LOW
static unsigned long s_last_motion_ms = 0;
static unsigned long s_last_note_ms   = 0;
static bool          s_ever_motion    = false;

void
presence_init() {
#if PIR_PIN == 16
    // GPIO16 has its own pull-down; an unplugged sensor then reads "no motion".
    pinMode(PIR_PIN, INPUT_PULLDOWN_16);
#else
    pinMode(PIR_PIN, INPUT);
#endif
    Serial.printf("[pir] sensor on GPIO%d\n", PIR_PIN);
}

void
presence_loop() {
    unsigned long now  = millis();
    bool          high = digitalRead(PIR_PIN) == HIGH;

    if (!high) {
        s_high_since_ms = 0;
        if (s_motion) {
            s_motion = false;
            Serial.println("[pir] clear");
        }
        return;
    }

    if (s_high_since_ms == 0) {
        s_high_since_ms = now ? now : 1;
        return;
    }
    if (now - s_high_since_ms < DEBOUNCE_MS) return;

    if (!s_motion) {
        s_motion = true;
        Serial.println("[pir] motion");
    }

    s_last_motion_ms = now;
    s_ever_motion    = true;

    if (now - s_last_note_ms >= NOTE_INTERVAL_MS) {
        s_last_note_ms = now;
        inactivity_note_activity();
    }
}

bool
presence_recent() {
    if (!s_ever_motion) return false;
    return s_motion || millis() - s_last_motion_ms < PRESENCE_ICON_HOLD_MS;
}

#else // !PIR_PIN

void
presence_init() {}
void
presence_loop() {}
bool
presence_recent() {
    return false;
}

#endif
