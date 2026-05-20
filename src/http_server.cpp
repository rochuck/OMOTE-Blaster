#include "http_server.h"
#include "blaster_state.h"
#include "ir_sender.h"
#include "status_led.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

static ESP8266WebServer* s_server = nullptr;

static void
send_json(int code, const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    // The ESP8266 has only a handful of TCP PCBs. Tell the client to close after
    // each response so connections can't pile up in keep-alive/TIME_WAIT and
    // exhaust the pool (which manifests as the server hanging after a few sends).
    s_server->sendHeader("Connection", "close");
    s_server->send(code, "application/json", body);
}

static void
send_error(int code, const char* msg) {
    JsonDocument doc;
    doc["ok"]    = false;
    doc["error"] = msg;
    send_json(code, doc);
}

static void
handle_status() {
    status_led_pulse_cmd();
    JsonDocument doc;
    doc["ok"]      = true;
    doc["service"] = "omote-blaster"; // identity check the remote validates
    doc["version"] = BLASTER_VERSION;
    doc["uptime"]  = (uint32_t) (millis() / 1000);
    doc["rssi"]    = WiFi.RSSI();
    doc["ip"]      = WiFi.localIP().toString();
    // Display-facing state pushed by the remote (see blaster_state).
    doc["scene"]          = blaster_state_get_scene();
    doc["lastCommand"]    = blaster_state_get_last_command();
    doc["lastCommandAgo"] = blaster_state_last_command_age_s();
    send_json(200, doc);
}

static void
handle_send() {
    if (!s_server->hasArg("plain")) {
        send_error(400, "missing JSON body");
        return;
    }
    const String& body = s_server->arg("plain");
    if (body.length() > 512) { // room for optional scene/name strings
        send_error(413, "body too large");
        return;
    }

    JsonDocument         req;
    DeserializationError err = deserializeJson(req, body);
    if (err) {
        send_error(400, "JSON parse error");
        return;
    }
    if (!req["protocol"].is<int>()) {
        send_error(400, "protocol (int) required");
        return;
    }
    if (!req["data"].is<const char*>()) {
        send_error(400, "data (string) required");
        return;
    }

    int    protocol = req["protocol"].as<int>();
    String data     = req["data"].as<const char*>();
    int    nbits    = req["nbits"] | 0;   // 0 == "use default"
    int    repeat   = req["repeat"] | -1; // -1 == "use default"

    IrSendResult r = ir_sender_send(protocol, data, nbits, repeat);
    if (!r.ok) {
        send_error(500, r.error ? r.error : "send failed");
        return;
    }

    // Display state: scene (optional) and a label for "last command". Prefer the
    // human-readable name the remote sends; fall back to the raw code when absent.
    if (req["scene"].is<const char*>()) {
        blaster_state_set_scene(req["scene"].as<const char*>());
    }
    const char* name = req["name"] | "";
    blaster_state_set_last_command(name[0] ? String(name) : data);

    Serial.printf("[rx] /send scene=\"%s\" cmd=\"%s\" proto=%d data=%s\n",
                  blaster_state_get_scene().c_str(),
                  blaster_state_get_last_command().c_str(), protocol, data.c_str());

    status_led_pulse_cmd();
    JsonDocument resp;
    resp["ok"] = true;
    send_json(200, resp);
}

static void
handle_scene() {
    if (!s_server->hasArg("plain")) {
        send_error(400, "missing JSON body");
        return;
    }
    const String& body = s_server->arg("plain");
    if (body.length() > 256) {
        send_error(413, "body too large");
        return;
    }
    JsonDocument         req;
    DeserializationError err = deserializeJson(req, body);
    if (err) {
        send_error(400, "JSON parse error");
        return;
    }
    if (!req["scene"].is<const char*>()) {
        send_error(400, "scene (string) required");
        return;
    }
    blaster_state_set_scene(req["scene"].as<const char*>());

    Serial.printf("[rx] /scene scene=\"%s\"\n", blaster_state_get_scene().c_str());

    status_led_pulse_cmd();
    JsonDocument resp;
    resp["ok"] = true;
    send_json(200, resp);
}

static void
handle_reset() {
    status_led_pulse_cmd();
    JsonDocument doc;
    doc["ok"] = true;
    send_json(200, doc);
    s_server->client().stop();
    delay(100);
    ESP.restart();
}

static void
handle_not_found() {
    send_error(404, "not found");
}

void
http_server_begin(uint16_t port) {
    s_server = new ESP8266WebServer(port);
    s_server->keepAlive(false); // close each connection after responding; see send_json
    s_server->on("/status", HTTP_GET, handle_status);
    s_server->on("/send", HTTP_POST, handle_send);
    s_server->on("/scene", HTTP_POST, handle_scene);
    s_server->on("/reset", HTTP_POST, handle_reset);
    s_server->onNotFound(handle_not_found);
    s_server->begin();
    Serial.printf("[http] app server listening on port %u\n", port);
}

void
http_server_loop() {
    if (s_server) s_server->handleClient();
}
