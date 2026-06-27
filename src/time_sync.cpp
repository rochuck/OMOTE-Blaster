#include "time_sync.h"

#include <Arduino.h>
#include <time.h>

// America/Edmonton (Mountain Time): MST (UTC-7) with MDT DST, transitions on the
// US schedule (2nd Sunday of March, 1st Sunday of November).
static const char* kTZ = "MST7MDT,M3.2.0,M11.1.0";

// Anything before ~2020-09 means NTP hasn't synced yet (the RTC starts near 0).
static const time_t kEpochSanityFloor = 1600000000;

void
time_sync_begin() {
    // ESP8266 core's TZ-string overload: sets the timezone and starts SNTP.
    // time() returns UTC regardless of the configured zone.
    configTime(kTZ, "pool.ntp.org", "time.nist.gov");
    Serial.printf("[time] SNTP started, tz=%s\n", kTZ);
}

bool
time_sync_valid() {
    return time(nullptr) > kEpochSanityFloor;
}

uint32_t
time_sync_epoch() {
    return (uint32_t) time(nullptr);
}
