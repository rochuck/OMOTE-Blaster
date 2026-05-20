#pragma once

enum WifiLedMode {
    WIFI_LED_OFF,
    WIFI_LED_SLOW_BLINK, // portal / connecting (500 ms period)
    WIFI_LED_FAST_BLINK, // OTA failure (100 ms period)
    WIFI_LED_SOLID
};

// All four functions are unconditionally callable. Pin presence is controlled
// at build time: undefining LED_WIFI_PIN or LED_CMD_PIN compiles out the
// corresponding logic (used by the ESP-01 build, which has no spare GPIOs).
void
status_led_init();
void
status_led_set_wifi(WifiLedMode mode);
void
status_led_pulse_cmd(); // restart the LED_CMD_HOLD_MS latch
void
status_led_loop(); // call from main loop
