#include "status_led.h"
#include <Arduino.h>

static uint8_t       s_wifi_pin     = 0;
static uint8_t       s_cmd_pin      = 0;
static WifiLedMode   s_wifi_mode    = WIFI_LED_OFF;
static unsigned long s_cmd_until_ms = 0;

static void
write_wifi(bool on) {
    digitalWrite(s_wifi_pin, on ? HIGH : LOW);
}

void
status_led_init(uint8_t wifi_pin, uint8_t cmd_pin) {
    s_wifi_pin = wifi_pin;
    s_cmd_pin  = cmd_pin;
    pinMode(s_wifi_pin, OUTPUT);
    pinMode(s_cmd_pin, OUTPUT);
    digitalWrite(s_wifi_pin, LOW);
    digitalWrite(s_cmd_pin, LOW);
}

void
status_led_set_wifi(WifiLedMode mode) {
    s_wifi_mode = mode;
}

void
status_led_pulse_cmd() {
    s_cmd_until_ms = millis() + LED_CMD_HOLD_MS;
    digitalWrite(s_cmd_pin, HIGH);
}

void
status_led_loop() {
    unsigned long now = millis();

    switch (s_wifi_mode) {
    case WIFI_LED_OFF:
        write_wifi(false);
        break;
    case WIFI_LED_SOLID:
        write_wifi(true);
        break;
    case WIFI_LED_SLOW_BLINK:
        write_wifi((now / 500) & 1);
        break;
    case WIFI_LED_FAST_BLINK:
        write_wifi((now / 100) & 1);
        break;
    }

    if (s_cmd_until_ms != 0 && (long) (now - s_cmd_until_ms) >= 0) {
        digitalWrite(s_cmd_pin, LOW);
        s_cmd_until_ms = 0;
    }
}
