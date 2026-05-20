#include "blaster_state.h"

static String        s_scene;
static String        s_last_command;
static unsigned long s_last_command_millis = 0;
static uint32_t      s_scene_version       = 0;
static uint32_t      s_scene_receipts      = 0;

void
blaster_state_set_scene(const String& scene) {
    s_scene_receipts++;           // every push is an edge, even unchanged ones
    if (scene == s_scene) return; // value unchanged: no scene_version bump
    s_scene = scene;
    s_scene_version++;            // edge the display can watch
}

const String&
blaster_state_get_scene() {
    return s_scene;
}

void
blaster_state_set_last_command(const String& name) {
    s_last_command       = name;
    s_last_command_millis = millis();
}

const String&
blaster_state_get_last_command() {
    return s_last_command;
}

unsigned long
blaster_state_last_command_millis() {
    return s_last_command_millis;
}

uint32_t
blaster_state_scene_version() {
    return s_scene_version;
}

uint32_t
blaster_state_scene_receipt_version() {
    return s_scene_receipts;
}

uint32_t
blaster_state_last_command_age_s() {
    if (s_last_command_millis == 0) return 0;
    return (uint32_t) ((millis() - s_last_command_millis) / 1000);
}
