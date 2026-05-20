#include "status_led.h"
#include <Arduino.h>

#ifdef LED_WIFI_PIN
static WifiLedMode s_wifi_mode = WIFI_LED_OFF;
#endif

#ifdef LED_CMD_PIN
static unsigned long s_cmd_until_ms = 0;
#endif

void
status_led_init() {
#ifdef LED_WIFI_PIN
    pinMode(LED_WIFI_PIN, OUTPUT);
    digitalWrite(LED_WIFI_PIN, LOW);
#endif
#ifdef LED_CMD_PIN
    pinMode(LED_CMD_PIN, OUTPUT);
    digitalWrite(LED_CMD_PIN, LOW);
#endif
}

void
status_led_set_wifi(WifiLedMode mode) {
#ifdef LED_WIFI_PIN
    s_wifi_mode = mode;
#else
    (void) mode;
#endif
}

void
status_led_pulse_cmd() {
#ifdef LED_CMD_PIN
    s_cmd_until_ms = millis() + LED_CMD_HOLD_MS;
    digitalWrite(LED_CMD_PIN, HIGH);
#endif
}

void
status_led_loop() {
#if defined(LED_WIFI_PIN) || defined(LED_CMD_PIN)
    unsigned long now = millis();
#endif

#ifdef LED_WIFI_PIN
    switch (s_wifi_mode) {
    case WIFI_LED_OFF:
        digitalWrite(LED_WIFI_PIN, LOW);
        break;
    case WIFI_LED_SOLID:
        digitalWrite(LED_WIFI_PIN, HIGH);
        break;
    case WIFI_LED_SLOW_BLINK:
        digitalWrite(LED_WIFI_PIN, (now / 500) & 1);
        break;
    case WIFI_LED_FAST_BLINK:
        digitalWrite(LED_WIFI_PIN, (now / 100) & 1);
        break;
    }
#endif

#ifdef LED_CMD_PIN
    if (s_cmd_until_ms != 0 && (long) (now - s_cmd_until_ms) >= 0) {
        digitalWrite(LED_CMD_PIN, LOW);
        s_cmd_until_ms = 0;
    }
#endif
}
