#include "inactivity.h"

#include "blaster_state.h"
#include "display.h"
#include "ir_sender.h"

#include <Arduino.h>

// Power off after this long with no user action, warning for the final stretch.
// NOTE: set low here for testing; production value is 60UL*60UL*1000UL (1 hour).
// It must be > COUNTDOWN_MS to leave a normal (non-warning) window; if it isn't,
// inactivity_loop() clamps the countdown so the timer still behaves sanely.
static constexpr unsigned long INACTIVITY_TIMEOUT_MS = 2UL * 60UL * 1000UL; // 1 min (test)
static constexpr unsigned long COUNTDOWN_MS          = 60UL * 1000UL;       // 60 s

// Protocol numbers as understood by ir_sender_send (decode_type_t / the OMOTE
// IR_PROTOCOL_* enum, which share numbering). SHARP=14, GLOBALCACHE=31.
static constexpr int PROTO_SHARP       = 14;
static constexpr int PROTO_GLOBALCACHE = 31;

// Discrete power-off codes, copied verbatim from OMOTE-Firmware:
//   Sharp TV  -> device_sharpTV.cpp   (SHARP 0x474A)
//   Marantz   -> device_marantzAmp.cpp (GlobalCache timing)
//   Apple TV  -> device_appleTV.cpp    (GlobalCache timing)
// nbits/repeat are left to ir_sender_send's protocol defaults (what the remote
// relied on), so we pass 0/-1 below.
static const char SHARP_OFF[]   = "0x474A";
static const char MARANTZ_OFF[] = "36000,1,1,32,32,32,32,32,32,64,32,32,32,32,32,32,161,32,32,32,64,"
                                  "32,32,64,32,32,32,32,32,32,32,32,32,32,64,64,2731,32,32,32,32,32,"
                                  "32,64,32,32,32,32,32,32,161,32,32,32,64,32,32,64,32,32,32,32,32,"
                                  "32,32,32,32,32,64,64,1200";
static const char APPLETV_OFF[] = "38380,1,69,347,173,22,65,22,22,22,65,22,22,22,22,22,65,22,65,22,"
                                  "65,22,65,22,65,22,65,22,22,22,22,22,22,22,22,22,65,22,22,22,65,22,"
                                  "22,22,65,22,22,22,65,22,22,22,22,22,22,22,65,22,65,22,65,22,65,22,"
                                  "65,22,65,22,65,22,1397,347,87,22,3692";

// Last user action, the baseline for the inactivity window. Seeded on first loop.
static unsigned long s_last_activity_ms = 0;
static bool          s_seeded           = false;
// True while the countdown warning is on screen. s_last_drawn_secs avoids
// redrawing the (slow I2C) panel except when the displayed number changes.
static bool s_counting        = false;
static int  s_last_drawn_secs = -1;

static bool
scene_is_active() {
    const String& scene = blaster_state_get_scene();
    return scene.length() && !scene.equalsIgnoreCase("Off");
}

static void
clear_countdown() {
    if (!s_counting) return;
    s_counting        = false;
    s_last_drawn_secs = -1;
    display_clear_countdown();
}

// Fire the power-off sequence and reflect "Off" in our state so the remote
// reconciles on its next poll. Order mirrors OMOTE-Firmware's scene_allOff.
static void
do_auto_off() {
    ir_sender_send(PROTO_SHARP, SHARP_OFF, 0, -1); // Sharp TV
    delay(10);
    ir_sender_send(PROTO_GLOBALCACHE, MARANTZ_OFF, 0, -1); // Marantz
    delay(10);
    ir_sender_send(PROTO_GLOBALCACHE, MARANTZ_OFF, 0, -1); // Marantz (repeat, to be sure)
    delay(10);
    ir_sender_send(PROTO_GLOBALCACHE, APPLETV_OFF, 0, -1); // Apple TV

    Serial.println("[inactivity] timeout reached; powered off, scene -> Off");

    // Push a COMPLETE, valid Off state tuple, not just the scene name. The
    // remotes reconcile via GET /state and need (scene, guiName, guiList,
    // lastIndex) to resolve to a real GUI; setting only the scene leaves the
    // stale GUI fields from the last active scene, so the remote's apply bails
    // half-done, never shows Off, and bounces the old scene back to us — which
    // re-arms this timer. Selecting "Off" on a remote lands it on the
    // scene-selection GUI in the main list (OMOTE-Firmware sceneHandler.cpp),
    // so that's the tuple we replay: MAIN_GUI_LIST (0), "Scene selection" (idx 0).
    blaster_state_set_last_command("Auto Off");
    blaster_state_set_gui("Off",
                          "Scene selection",
                          /*guiList=MAIN_GUI_LIST*/ 0,
                          /*lastIndex*/ 0);

    s_counting        = false;
    s_last_drawn_secs = -1;
    display_clear_countdown(); // drops countdown mode and repaints the Off scene
}

void
inactivity_note_activity() {
    // Just stamp the time; inactivity_loop() observes the reset and tears down
    // any countdown, keeping all display calls on the main loop.
    s_last_activity_ms = millis();
    s_seeded           = true;
}

void
inactivity_loop() {
    if (!s_seeded) {
        s_seeded           = true;
        s_last_activity_ms = millis();
    }

    // Off / no scene: nothing to time. Keep the baseline fresh so the full hour
    // starts from the moment a scene becomes active again.
    if (!scene_is_active()) {
        clear_countdown();
        s_last_activity_ms = millis();
        return;
    }

    // Countdown is the final stretch of the window. Clamp it so there's always
    // a non-warning window left (and so the subtraction below can't underflow)
    // even if someone sets INACTIVITY_TIMEOUT_MS <= COUNTDOWN_MS for testing.
    unsigned long countdown_ms = COUNTDOWN_MS;
    if (countdown_ms >= INACTIVITY_TIMEOUT_MS) { countdown_ms = INACTIVITY_TIMEOUT_MS / 2; }
    unsigned long warn_at_ms = INACTIVITY_TIMEOUT_MS - countdown_ms;

    unsigned long elapsed = millis() - s_last_activity_ms;

    if (elapsed >= INACTIVITY_TIMEOUT_MS) {
        do_auto_off();
        s_last_activity_ms = millis();
        return;
    }

    // Not into the warning window yet. If activity reset us back out of it,
    // tear the countdown down and return to showing the scene.
    if (elapsed < warn_at_ms) {
        clear_countdown();
        return;
    }

    // Final stretch: show the flashing countdown. ceil so it reads 60..1.
    int secs = (int) ((INACTIVITY_TIMEOUT_MS - elapsed + 999) / 1000);
    if (secs != s_last_drawn_secs) {
        s_last_drawn_secs = secs;
        s_counting        = true;
        display_show_countdown(secs, secs & 1); // invert flips each second -> flash
    }
}
