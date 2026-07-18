/*
 * decode.cpp — turn a validated payload into a typed Telemetry value.
 *
 * Framing and CRC are handled upstream, so a payload arriving here is already
 * structurally sound. What remains is semantic validation: the protocol version
 * must be one we speak, the type must be known, and the payload must be long
 * enough for the body that type implies.
 *
 * Validation completes before any field of `out` is written, so a caller can
 * never observe a half-filled Telemetry after an error.
 *
 * Status: not yet implemented.
 */

#include <sharak/decode.hpp>
#include <sharak/protocol.h>

namespace sharak {

DecodeStatus decode(const uint8_t *payload, std::size_t len, Telemetry &out)
{
    (void)payload;
    (void)len;
    (void)out;
    return DecodeStatus::BadLength;
}

}  // namespace sharak
