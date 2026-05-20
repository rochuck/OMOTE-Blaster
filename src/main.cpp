#include <Arduino.h>

#include "http_server.h"
#include "ir_sender.h"
#include "mdns_service.h"
#include "ota.h"
#include "status_led.h"
#include "wifi_setup.h"

void
setup() {
    // First thing: park the IR LED in its OFF state. The LED is overdriven, so
    // every millisecond the FET gate spends floating after reset is a risk.
    ir_sender_init();

    Serial.begin(115200);
    delay(100);
    Serial.printf("\n[boot] OMOTE-Blaster %s\n", BLASTER_VERSION);

    status_led_init();

    wifi_setup_begin(BLASTER_AP_NAME, BLASTER_HOSTNAME);
    mdns_service_begin(BLASTER_HOSTNAME, BLASTER_HTTP_PORT, BLASTER_OTA_PORT);
    http_server_begin(BLASTER_HTTP_PORT);
    ota_begin(BLASTER_OTA_PORT);

    Serial.println("[boot] ready");
}

void
loop() {
    mdns_service_loop();
    http_server_loop();
    ota_loop();
    status_led_loop();
}
