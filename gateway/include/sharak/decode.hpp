#ifndef SHARAK_GATEWAY_DECODE_HPP
#define SHARAK_GATEWAY_DECODE_HPP

/*
 * decode.hpp — the C++ side of the wire contract.
 *
 * The frame decoder in protocol.c has already done framing and CRC by the time
 * we get here, so a payload arriving at this layer is structurally valid. This
 * stage's only job is to turn those raw bytes into a typed C++ value, choosing
 * the right body layout from the `type` byte in the envelope.
 *
 * It is deliberately thin and free of I/O: no sockets, no storage, no logging.
 * That keeps it trivially unit-testable and keeps the wire format understood in
 * exactly one place.
 */

#include <cstddef>
#include <cstdint>

namespace sharak {

/* Which message arrived. Mirrors SK_TYPE_* on the wire. */
enum class Kind {
    Accel,
    Temp,
    Current,
};

/* Why a decode failed. Ok means `out` was written; on any error `out` is
 * untouched, so a caller can never read half-filled data. */
enum class DecodeStatus {
    Ok,
    BadLength,      /* payload too short for its declared type   */
    BadVersion,     /* envelope ver is not SK_VER                */
    UnknownType,    /* type byte is not one we know             */
};

/*
 * One decoded reading. `kind` selects which body fields are meaningful:
 *   Accel   -> x_mg, y_mg, z_mg
 *   Temp    -> temp_centi_c
 *   Current -> current_ma
 * Fields outside the active kind are left zeroed.
 */
struct Telemetry {
    Kind     kind{};
    uint16_t seq{};

    int32_t  x_mg{};
    int32_t  y_mg{};
    int32_t  z_mg{};

    int16_t  temp_centi_c{};

    int32_t  current_ma{};
};

/*
 * Decode one validated payload into `out`.
 * Returns DecodeStatus::Ok and fills `out` on success; on any error returns the
 * reason and leaves `out` unmodified.
 */
DecodeStatus decode(const uint8_t *payload, std::size_t len, Telemetry &out);

}  // namespace sharak

#endif  /* SHARAK_GATEWAY_DECODE_HPP */
