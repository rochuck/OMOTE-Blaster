// ESP8266 port of OMOTE-Firmware/hardware/ESP32/ota_hal_esp32.cpp.
// Same shape: HTTP server on its own port, multipart firmware upload at
// POST /update, dev-machine pushes via curl (tools/ota_upload.py). All
// connections are dev-machine → device, which is what makes it work
// across an IoT VLAN.

#include "ota.h"

#if (ENABLE_OTA == 1)

#include "display.h"
#include "status_led.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <Updater.h>

static ESP8266WebServer* s_ota_server = nullptr;
static size_t            s_total      = 0;
static size_t            s_written    = 0;

static void
handle_post() {
    s_ota_server->sendHeader("Connection", "close");
    if (Update.hasError()) {
        s_ota_server->send(200, "text/plain", "FAIL");
        Serial.println("[ota] failed");
        status_led_set_wifi(WIFI_LED_FAST_BLINK);
        display_show_ota_result(false);
    } else {
        s_ota_server->send(200, "text/plain", "OK");
        Serial.println("[ota] success — rebooting");
        display_show_ota_result(true);
        delay(100);
        ESP.restart();
    }
}

static void
handle_upload() {
    HTTPUpload& upload = s_ota_server->upload();

    if (upload.status == UPLOAD_FILE_START) {
        s_written = 0;
        s_total   = s_ota_server->header("Content-Length").toInt();
        Serial.printf("[ota] receiving, expected %u bytes\n", (unsigned) s_total);
        status_led_set_wifi(WIFI_LED_SOLID);
        // ESP8266 wants a real size up-front. Use Content-Length when
        // available, else fall back to the max free sketch space (the
        // standard ESP8266HTTPUpdateServer pattern).
        uint32_t size = s_total > 0 ? (uint32_t) s_total : ((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
        if (!Update.begin(size, U_FLASH)) { Update.printError(Serial); }
        display_show_ota_progress(0, s_total);

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }
        s_written += upload.currentSize;
        display_show_ota_progress(s_written, s_total);

    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[ota] %u bytes written\n", (unsigned) upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

void
ota_begin(uint16_t port) {
    s_ota_server = new ESP8266WebServer(port);
    // ESP8266 collectHeaders is variadic — pass header names directly,
    // not as (array, count) like the ESP32 API.
    s_ota_server->collectHeaders("Content-Length");
    s_ota_server->on("/update", HTTP_POST, handle_post, handle_upload);
    s_ota_server->begin();
    Serial.printf("[ota] HTTP server ready on port %u (/update)\n", port);
}

void
ota_loop() {
    if (s_ota_server) s_ota_server->handleClient();
}

#endif // ENABLE_OTA == 1
