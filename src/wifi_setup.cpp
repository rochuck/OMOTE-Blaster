#include "wifi_setup.h"
#include "status_led.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

void
wifi_setup_begin(const char* ap_name, const char* hostname) {
    pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
    delay(20); // settle the pull-up before sampling
    bool force_portal = (digitalRead(PORTAL_BUTTON_PIN) == LOW);

    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);

    status_led_set_wifi(WIFI_LED_SLOW_BLINK);

    WiFiManager wm;
    wm.setConfigPortalTimeout(0); // block forever — the blaster is useless without WiFi
    wm.setConnectTimeout(20);

    if (force_portal) {
        Serial.println("[wifi] portal button held — forcing config portal");
        wm.startConfigPortal(ap_name);
    } else {
        Serial.printf("[wifi] connecting (autoConnect, AP fallback = %s)\n", ap_name);
        if (!wm.autoConnect(ap_name)) {
            Serial.println("[wifi] autoConnect failed; restarting");
            delay(1000);
            ESP.restart();
        }
    }

    Serial.printf("[wifi] connected, ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    status_led_set_wifi(WIFI_LED_SOLID);
}
