#include "mdns_service.h"
#include <Arduino.h>
#include <ESP8266mDNS.h>

void
mdns_service_begin(const char* hostname, uint16_t app_port, uint16_t ota_port) {
    if (!MDNS.begin(hostname)) {
        Serial.println("[mdns] failed to start");
        return;
    }
    // App service — what the OMOTE remote browses for.
    MDNS.addService("omote-blaster", "tcp", app_port);
    MDNS.addService("http", "tcp", app_port);
#if (ENABLE_OTA == 1)
    MDNS.addService("http", "tcp", ota_port);
#endif
    Serial.printf("[mdns] %s.local up (app=%u, ota=%u)\n", hostname, app_port, ota_port);
}

void
mdns_service_loop() {
    MDNS.update();
}
