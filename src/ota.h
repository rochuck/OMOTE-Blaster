#pragma once

#include <stdint.h>

#if (ENABLE_OTA == 1)
void
ota_begin(uint16_t port);
void
ota_loop();
#else
inline void
ota_begin(uint16_t) {}
inline void
ota_loop() {}
#endif
