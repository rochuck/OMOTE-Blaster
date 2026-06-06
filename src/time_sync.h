#pragma once

#include <stdint.h>

// Wall-clock time over NTP. The blaster is the time authority for the remote(s):
// it syncs UTC from NTP in the background and serves the current epoch as part of
// GET /state, so the remote can show a clock without running NTP itself.
//
// The timezone is set to America/Edmonton (Mountain Time, DST-aware) for the
// blaster's own local time, but the served epoch is always UTC — the remote
// applies the timezone at render time.

// Configure SNTP + timezone. Call once from setup() AFTER WiFi is connected.
// Non-blocking: time becomes valid a few hundred ms later once the first NTP
// packet arrives, and SNTP auto-resyncs periodically thereafter.
void
time_sync_begin();

// True once the clock has been set from NTP (epoch past a ~2020 sanity floor),
// so we never serve a bogus pre-sync epoch.
bool
time_sync_valid();

// Current UTC time as Unix epoch seconds. Only meaningful when time_sync_valid().
uint32_t
time_sync_epoch();
