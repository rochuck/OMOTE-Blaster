#pragma once

#include <stdint.h>

void
mdns_service_begin(const char* hostname, uint16_t app_port, uint16_t ota_port);
void
mdns_service_loop();
