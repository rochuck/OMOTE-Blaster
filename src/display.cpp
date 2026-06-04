#include "display.h"

#if (ENABLE_DISPLAY == 1)

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Wire.h>

#include "apple_tv_logo.h"
#include "blaster_state.h"
#include "kodi_logo.h"
#include "lyrion_logo.h"
#include "off_logo.h"
#include "zethus_logo.h"

// 128x32 panel. -1 = no hardware reset pin (shared with the MCU reset on most
// cheap modules). Default I2C address for these boards is 0x3C.
static constexpr uint8_t DISPLAY_WIDTH   = 128;
static constexpr uint8_t DISPLAY_HEIGHT  = 32;
static constexpr uint8_t DISPLAY_ADDRESS = 0x3C;

static Adafruit_SSD1306 s_display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
static bool             s_ready = false;

// While true, display_loop() yields the panel to the inactivity countdown and
// skips its own refresh/sleep logic. Cleared by display_clear_countdown().
static bool s_countdown_active = false;

// Panel power state, shared between display_loop() and the wake helper. The
// sleep timeout cuts the SSD1306 charge pump (DISPLAYOFF); anything that needs
// to paint while we might be asleep must wake() first or it draws into hidden
// GDDRAM. millis() of the last command/scene edge, used to time the sleep.
static bool          s_asleep           = false;
static unsigned long s_last_activity_ms = 0;

// Turn the panel back on and reset the sleep timer. Idempotent. Used both by
// display_loop on a new command/scene edge and by the OTA screens, which can
// arrive long after the panel has slept.
static void
display_wake() {
    if (s_asleep) {
        s_display.ssd1306_command(SSD1306_DISPLAYON);
        s_asleep = false;
    }
    s_last_activity_ms = millis();
}

void
display_init() {
    Wire.begin(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);

    if (!s_display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
        Serial.println("[display] SSD1306 not found");
        return;
    }
    s_ready = true;

    s_display.clearDisplay();
    s_display.drawBitmap(0, 0, ZETHUS_LOGO_BITS, ZETHUS_LOGO_WIDTH,
                         ZETHUS_LOGO_HEIGHT, SSD1306_WHITE);
    s_display.display();

    Serial.println("[display] ready");
}

// Render the active scene plus the most recent IR command. Scene rides the top
// row in the small built-in font; the command label fills the rest at size 2 so
// it reads across the room as commands fly by.
static void
display_show_command(const String& scene, const String& command) {
    if (!s_ready) return;

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.setFont(nullptr);

    s_display.setTextSize(1);
    s_display.setCursor(0, 0);
    s_display.print(scene.length() ? scene : String("Blaster"));

    // Command label in a TTF-derived GFX font. GFX fonts draw from the baseline,
    // so y is the baseline row; 28 sits the ~13px-tall glyphs under the scene line.
    s_display.setFont(&FreeSansBold9pt7b);
    s_display.setCursor(0, 28);
    s_display.print(command);
    s_display.setFont(nullptr); // restore default for other screens

    s_display.display();
}

// Big centered scene name, shown after a command's dwell time elapses. These
// labels stand in for per-scene icons we'll swap in later, so they get the
// whole panel in the large GFX font.
// Some scenes get a glyph instead of their name. Returns true if it painted one,
// so display_show_scene() can skip its text path. Stand-in for a future table of
// per-scene icons.
static bool
display_show_scene_icon(const String& scene) {
    // Each entry maps a scene name to a centered 1-bit wordmark bitmap.
    const unsigned char* bits = nullptr;
    int16_t              w = 0, h = 0;
    if (scene.equalsIgnoreCase("Apple TV")) {
        bits = APPLE_TV_LOGO_BITS;
        w    = APPLE_TV_LOGO_WIDTH;
        h    = APPLE_TV_LOGO_HEIGHT;
    } else if (scene.equalsIgnoreCase("Kodi")) {
        bits = KODI_LOGO_BITS;
        w    = KODI_LOGO_WIDTH;
        h    = KODI_LOGO_HEIGHT;
    } else if (scene.equalsIgnoreCase("Lyrion") ||
               scene.equalsIgnoreCase("LMS")) {
        bits = LYRION_LOGO_BITS;
        w    = LYRION_LOGO_WIDTH;
        h    = LYRION_LOGO_HEIGHT;
    } else if (scene.equalsIgnoreCase("Off")) {
        bits = OFF_LOGO_BITS;
        w    = OFF_LOGO_WIDTH;
        h    = OFF_LOGO_HEIGHT;
    } else {
        return false;
    }

    int16_t lx = (DISPLAY_WIDTH - w) / 2;
    int16_t ly = (DISPLAY_HEIGHT - h) / 2;

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.drawBitmap(lx, ly, bits, w, h, SSD1306_WHITE);
    s_display.display();
    return true;
}

static void
display_show_scene(const String& scene) {
    if (!s_ready) return;

    if (display_show_scene_icon(scene)) return;

    String label = scene.length() ? scene : String("Blaster");

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);

    s_display.setFont(&FreeSansBold9pt7b);

    // Center the label: getTextBounds gives the rendered extent (chosen font and
    // all) so we can place the cursor at the left/baseline that lands the glyphs
    // in the middle.
    int16_t  x1, y1;
    uint16_t w, h;
    s_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (DISPLAY_WIDTH - (int16_t)w) / 2 - x1;
    int16_t y = (DISPLAY_HEIGHT - (int16_t)h) / 2 - y1;
    s_display.setCursor(x, y);
    s_display.print(label);
    s_display.setFont(nullptr); // restore default for other screens

    s_display.display();
}

void
display_loop() {
    // How long the IR command label stays up before we swap to the scene name.
    static constexpr unsigned long COMMAND_DWELL_MS = 2000;

    // Power the panel off after this long with no command or scene change. The
    // SSD1306 has nothing to refresh, so a static scene name would otherwise sit
    // lit indefinitely and burn into the OLED. DISPLAYOFF cuts the charge pump,
    // so the panel draws ~uA and can't burn in. Any new activity wakes it.
    static constexpr unsigned long SLEEP_TIMEOUT_MS = 5UL * 60UL * 1000UL;

    // Shorter dwell for a "wake and show me the current scene" nudge — the
    // remote re-pushing the scene it's already on while we're asleep. We light
    // up briefly to confirm, then drop back to sleep.
    static constexpr unsigned long NUDGE_TIMEOUT_MS = 15UL * 1000UL;

    // Repaint only when a new /send lands. blaster_state stamps millis() on each
    // command, so a changed timestamp is our "new command arrived" edge — no
    // need to diff strings or redraw every loop.
    static unsigned long s_last_rendered_millis = 0;
    static bool          s_scene_shown          = true;
    static uint32_t      s_last_scene_version   = 0;
    static uint32_t      s_last_scene_receipt   = 0;

    // Active sleep timeout. Normally the full 5 min, but a same-scene wake nudge
    // shortens it to NUDGE_TIMEOUT_MS until the next real command/scene change.
    static unsigned long s_sleep_timeout_ms = SLEEP_TIMEOUT_MS;

    // Sleep bookkeeping. s_last_activity_ms / s_asleep live at file scope so the
    // OTA screens can wake the panel too; seed the activity stamp on the first
    // loop so the splash/status screen counts as activity.
    static bool s_seeded = false;
    if (!s_seeded) {
        s_seeded           = true;
        s_last_activity_ms = millis();
    }

    // The inactivity countdown owns the panel while it's up; don't fight it with
    // repaints or sleep. A user action after cancel clears the flag and arrives
    // as a normal command/scene edge below, which repaints through wake().
    if (s_countdown_active) return;

    // A scene change repaints immediately, even with no IR command, so pushing a
    // new /scene swaps the displayed logo right away, with the full sleep dwell.
    uint32_t scene_version = blaster_state_scene_version();
    uint32_t scene_receipt = blaster_state_scene_receipt_version();
    if (scene_version != s_last_scene_version) {
        s_last_scene_version = scene_version;
        s_last_scene_receipt = scene_receipt;
        s_scene_shown        = true;
        s_sleep_timeout_ms   = SLEEP_TIMEOUT_MS;
        display_wake();
        display_show_scene(blaster_state_get_scene());
        return;
    }

    // Re-pushing the scene we're already on is normally a no-op, but while the
    // panel is asleep we treat it as a "wake and show me the current scene"
    // nudge: light up, repaint, and use the short dwell so it sleeps again soon.
    if (scene_receipt != s_last_scene_receipt) {
        s_last_scene_receipt = scene_receipt;
        if (s_asleep) {
            s_scene_shown      = true;
            s_sleep_timeout_ms = NUDGE_TIMEOUT_MS;
            display_wake();
            display_show_scene(blaster_state_get_scene());
            return;
        }
    }

    unsigned long stamp = blaster_state_last_command_millis();
    if (stamp != 0 && stamp != s_last_rendered_millis) {
        s_last_rendered_millis = stamp;
        s_scene_shown          = false;
        s_sleep_timeout_ms     = SLEEP_TIMEOUT_MS;
        display_wake();
        display_show_command(blaster_state_get_scene(),
                             blaster_state_get_last_command());
        return;
    }

    // Once the command has been up for its dwell time, fall back to the big
    // scene name. Guarded so we paint it exactly once per command.
    if (!s_scene_shown && stamp != 0 &&
        millis() - s_last_rendered_millis >= COMMAND_DWELL_MS) {
        s_scene_shown = true;
        display_show_scene(blaster_state_get_scene());
    }

    // No activity for the timeout window: power the panel off until the next
    // command or scene change wakes it.
    if (!s_asleep && millis() - s_last_activity_ms >= s_sleep_timeout_ms) {
        s_display.ssd1306_command(SSD1306_DISPLAYOFF);
        s_asleep = true;
    }
}

void
display_show_connected(const String& ip) {
    if (!s_ready) return;

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);

    // Italic serif title, echoing the logo. Custom GFX fonts draw from the
    // baseline, so y is the baseline row, not the top of the glyphs.
    s_display.setFont(&FreeSerifBoldItalic9pt7b);
    s_display.setCursor(2, 15);
    s_display.print("Blaster Ready");

    // Back to the built-in 5x7 font for the IP line at the bottom.
    s_display.setFont(nullptr);
    s_display.setTextSize(1);
    s_display.setCursor(0, 24);
    s_display.print(ip);
    s_display.display();
}

void
display_show_ota_progress(size_t written, size_t total) {
    if (!s_ready) return;

    // An OTA can land long after the panel slept; force it on so the bar is
    // actually visible and not just drawn into a powered-down GDDRAM.
    display_wake();

    // Bar geometry: a full-width outlined rail under a title line, 2px inset so
    // the frame doesn't touch the panel edge.
    static constexpr int16_t BAR_X = 2;
    static constexpr int16_t BAR_Y = 18;
    static constexpr int16_t BAR_W = DISPLAY_WIDTH - 2 * BAR_X;
    static constexpr int16_t BAR_H = 12;
    static constexpr int16_t FILL_MAX = BAR_W - 2; // inside the 1px border

    // Without a Content-Length we can't show real progress; draw an empty rail.
    int16_t fill = 0;
    int     pct  = 0;
    if (total > 0) {
        if (written > total) written = total;
        fill = (int16_t)((uint32_t)written * FILL_MAX / total);
        pct  = (int)((uint32_t)written * 100 / total);
    }

    // Self-throttle: repaint only when the fill width changes. I2C is slow
    // (~5ms for a full 128x32 flush), and chunks arrive far faster than one
    // pixel of bar each, so this keeps the OLED from bottlenecking the flash.
    // written == 0 is the start of a transfer; always paint that frame so a
    // fresh upload shows an empty bar even if the previous one also ended at 0.
    static int16_t s_last_fill = -1;
    if (written != 0 && fill == s_last_fill) return;
    s_last_fill = fill;

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.setFont(nullptr);
    s_display.setTextSize(1);

    s_display.setCursor(0, 0);
    s_display.print("Updating");
    if (total > 0) {
        // Right-align the percentage on the title row (6px per char at size 1).
        String p = String(pct) + "%";
        s_display.setCursor(DISPLAY_WIDTH - (int16_t)p.length() * 6, 0);
        s_display.print(p);
    }

    // Pill: rounded rail with the corner radius pinned to half the height, and
    // a fill that's rounded to match. fillRoundRect needs a width of at least
    // 2*r+1 to render, so clamp the radius to the current fill width.
    static constexpr int16_t RAIL_R = BAR_H / 2;
    static constexpr int16_t FILL_R = (BAR_H - 2) / 2;
    s_display.drawRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, RAIL_R, SSD1306_WHITE);
    if (fill > 0) {
        int16_t r = fill < 2 * FILL_R + 1 ? fill / 2 : FILL_R;
        s_display.fillRoundRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, r, SSD1306_WHITE);
    }
    s_display.display();
}

void
display_show_ota_result(bool ok) {
    if (!s_ready) return;

    display_wake();

    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.setFont(&FreeSansBold9pt7b);

    String label = ok ? "Update OK" : "Update FAIL";
    int16_t  x1, y1;
    uint16_t w, h;
    s_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (DISPLAY_WIDTH - (int16_t)w) / 2 - x1;
    int16_t y = (DISPLAY_HEIGHT - (int16_t)h) / 2 - y1;
    s_display.setCursor(x, y);
    s_display.print(label);
    s_display.setFont(nullptr);
    s_display.display();
}

void
display_show_ap_mode(const String& ssid, const String& portal_ip) {
    if (!s_ready) return;

    // 128x32 at text size 1 is 21 chars x 4 rows. Three lines of join steps.
    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.setFont(nullptr);
    s_display.setTextSize(1);
    s_display.setCursor(0, 0);
    s_display.println("WiFi setup:");
    s_display.print("Join ");
    s_display.println(ssid);
    s_display.print("Open ");
    s_display.print(portal_ip);
    s_display.display();
}

void
display_show_countdown(int seconds, bool invert) {
    if (!s_ready) return;

    // By now the panel has been asleep for ~55 min; force it on.
    s_countdown_active = true;
    s_display.ssd1306_command(SSD1306_DISPLAYON);

    // size 4 of the built-in 5x7 font is ~32px tall — fills the panel height.
    s_display.setFont(nullptr);
    s_display.setTextSize(4);

    // Reverse video on alternate seconds: paint the whole panel white and draw
    // the digits in black; otherwise black panel, white digits.
    if (invert) {
        s_display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, SSD1306_WHITE);
        s_display.setTextColor(SSD1306_BLACK);
    } else {
        s_display.clearDisplay();
        s_display.setTextColor(SSD1306_WHITE);
    }

    String label = String(seconds);
    int16_t  x1, y1;
    uint16_t w, h;
    s_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (DISPLAY_WIDTH - (int16_t)w) / 2 - x1;
    int16_t y = (DISPLAY_HEIGHT - (int16_t)h) / 2 - y1;
    s_display.setCursor(x, y);
    s_display.print(label);

    s_display.setTextSize(1); // restore default for other screens
    s_display.display();
}

void
display_clear_countdown() {
    if (!s_ready) return;
    s_countdown_active = false;
    // Repaint whatever scene is now current (the live scene on cancel, "Off"
    // after an auto-off). display_loop resumes its normal duties next tick.
    display_show_scene(blaster_state_get_scene());
}

#else // ENABLE_DISPLAY

void
display_init() {}
void
display_loop() {}
void
display_show_connected(const String&) {}
void
display_show_ap_mode(const String&, const String&) {}
void
display_show_ota_progress(size_t, size_t) {}
void
display_show_ota_result(bool) {}
void
display_show_countdown(int, bool) {}
void
display_clear_countdown() {}

#endif
