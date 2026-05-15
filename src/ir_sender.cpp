#include "ir_sender.h"
#include <IRremoteESP8266.h>
#include <IRsend.h>

// Matches OMOTE: P-channel MOSFET high-side driver (DMG2301L), so the line is
// active-low — gate LOW turns the FET on. inverted=true, idle HIGH.
static IRsend irsend(IR_SEND_PIN, true);

void
ir_sender_init() {
    // Set the output latch HIGH *before* switching to OUTPUT so the pin never
    // gets driven LOW (which would turn the FET on) during the pinMode call.
    digitalWrite(IR_SEND_PIN, HIGH);
    pinMode(IR_SEND_PIN, OUTPUT);
    irsend.begin();
}

// Look up nbits/repeat defaults for the protocols the OMOTE actually uses.
// Ported from OMOTE-Firmware/hardware/ESP32/infrared_sender_hal_esp32.cpp:24-52.
// Returns true when defaults were found.
static bool
get_protocol_defaults(int protocol, uint16_t& nbits, uint16_t& repeat) {
    switch (protocol) {
    case RC5:
        nbits  = kRC5XBits;
        repeat = kNoRepeat;
        return true;
    case NEC:
        nbits  = kNECBits;
        repeat = kNoRepeat;
        return true;
    case SONY:
        nbits  = kSony20Bits;
        repeat = kSonyMinRepeat;
        return true;
    case JVC:
        nbits  = kJvcBits;
        repeat = kNoRepeat;
        return true;
    case SAMSUNG:
        nbits  = kSamsungBits;
        repeat = kNoRepeat;
        return true;
    case LG:
        nbits  = kLgBits;
        repeat = kNoRepeat;
        return true;
    case SANYO:
        nbits  = kSanyoLC7461Bits;
        repeat = kNoRepeat;
        return true;
    case SHARP:
        nbits  = kSharpBits;
        repeat = kNoRepeat;
        return true;
    case DENON:
        nbits  = kDenonBits;
        repeat = kNoRepeat;
        return true;
    case SHERWOOD:
        nbits  = kSherwoodBits;
        repeat = kSherwoodMinRepeat;
        return true;
    case LG2:
        nbits  = kLgBits;
        repeat = kNoRepeat;
        return true;
    case SAMSUNG36:
        nbits  = kSamsung36Bits;
        repeat = kNoRepeat;
        return true;
    default:
        return false;
    }
}

IrSendResult
ir_sender_send(int protocol, const String& dataStr, int nbits, int repeat) {
    if (dataStr.length() == 0) { return {false, "missing data"}; }

    // Parse "0xABCD", "0b1010", or decimal. strtoull with base=0 handles all three.
    char*    endp = nullptr;
    uint64_t data = strtoull(dataStr.c_str(), &endp, 0);
    if (endp == dataStr.c_str() || (endp && *endp != '\0')) { return {false, "data not a number"}; }

    uint16_t use_nbits  = (nbits > 0) ? (uint16_t) nbits : 0;
    uint16_t use_repeat = (repeat >= 0) ? (uint16_t) repeat : 0;

    if (use_nbits == 0) {
        uint16_t def_nbits = 0, def_repeat = 0;
        if (!get_protocol_defaults(protocol, def_nbits, def_repeat)) { return {false, "no defaults for protocol; nbits required"}; }
        use_nbits = def_nbits;
        if (repeat < 0) use_repeat = def_repeat;
    }

    Serial.printf("[IR] proto=%d data=0x%llx nbits=%u repeat=%u\n", protocol, (unsigned long long) data, use_nbits, use_repeat);

    bool ok = irsend.send((decode_type_t) protocol, data, use_nbits, use_repeat);
    // The LED is overdriven — force the line back to idle regardless of how
    // the library exited, so a misbehaving send can't leave the FET conducting.
    digitalWrite(IR_SEND_PIN, HIGH);
    if (!ok) { return {false, "IrSender.send rejected (unsupported protocol?)"}; }
    return {true, nullptr};
}
