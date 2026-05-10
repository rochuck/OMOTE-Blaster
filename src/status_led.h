#pragma once

#include <stdint.h>

enum WifiLedMode {
    WIFI_LED_OFF,
    WIFI_LED_SLOW_BLINK, // portal / connecting (500 ms period)
    WIFI_LED_FAST_BLINK, // OTA failure (100 ms period)
    WIFI_LED_SOLID
};

void
status_led_init(uint8_t wifi_pin, uint8_t cmd_pin);
void
status_led_set_wifi(WifiLedMode mode);
void
status_led_pulse_cmd(); // restart the LED_CMD_HOLD_MS latch
void
status_led_loop(); // call from main loop
