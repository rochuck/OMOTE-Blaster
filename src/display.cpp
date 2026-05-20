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
#include "zethus_logo.h"

// 128x32 panel. -1 = no hardware reset pin (shared with the MCU reset on most
// cheap modules). Default I2C address for these boards is 0x3C.
static constexpr uint8_t DISPLAY_WIDTH   = 128;
static constexpr uint8_t DISPLAY_HEIGHT  = 32;
static constexpr uint8_t DISPLAY_ADDRESS = 0x3C;

static Adafruit_SSD1306 s_display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
static bool             s_ready = false;

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

    // Center the label: getTextBounds gives the rendered extent so we can place
    // the cursor at the left/baseline that lands the glyphs in the middle.
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

    // Repaint only when a new /send lands. blaster_state stamps millis() on each
    // command, so a changed timestamp is our "new command arrived" edge — no
    // need to diff strings or redraw every loop.
    static unsigned long s_last_rendered_millis = 0;
    static bool          s_scene_shown          = true;
    static uint32_t      s_last_scene_version   = 0;

    // A scene change repaints immediately, even with no IR command, so pushing a
    // new /scene swaps the displayed logo right away.
    uint32_t scene_version = blaster_state_scene_version();
    if (scene_version != s_last_scene_version) {
        s_last_scene_version = scene_version;
        s_scene_shown        = true;
        display_show_scene(blaster_state_get_scene());
        return;
    }

    unsigned long stamp = blaster_state_last_command_millis();
    if (stamp != 0 && stamp != s_last_rendered_millis) {
        s_last_rendered_millis = stamp;
        s_scene_shown          = false;
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
    s_display.print("Omote Ready");

    // Back to the built-in 5x7 font for the IP line at the bottom.
    s_display.setFont(nullptr);
    s_display.setTextSize(1);
    s_display.setCursor(0, 24);
    s_display.print(ip);
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

#else // ENABLE_DISPLAY

void
display_init() {}
void
display_loop() {}
void
display_show_connected(const String&) {}
void
display_show_ap_mode(const String&, const String&) {}

#endif
