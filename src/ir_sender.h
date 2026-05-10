#pragma once

#include <Arduino.h>
#include <stdint.h>

struct IrSendResult {
    bool        ok;
    const char* error; // nullptr when ok
};

void
ir_sender_init();

// Sends an IR code. dataStr is "0xABCD" or decimal; nbits/repeat <= 0 means
// "use the protocol's default". Returns ok=false with a static error string
// on bad input or if the underlying library reports failure.
IrSendResult
ir_sender_send(int protocol, const String& dataStr, int nbits, int repeat);
