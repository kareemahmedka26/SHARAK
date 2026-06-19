/*
 * fleet_node.c — pure signal + frame-building logic for the fleet simulator.
 * No sockets, no I/O here (that is fleet_main.c), so this is host-unit-testable.
 */

#include "fleet_node.h"
#include <sharak/protocol.h>

/* Fixed wobble table (milli-g). Small, symmetric, integer-only. */
static const int32_t WOBBLE[8] = { 0, 5, 8, 5, 0, -5, -8, -5 };

void fleet_sample(uint32_t phase,
                  uint16_t *seq, int32_t *x_mg, int32_t *y_mg, int32_t *z_mg)
{
    *seq  = (uint16_t)(phase & 0xFFFFu);   /* rolling counter, wraps */
    *x_mg = WOBBLE[phase % 8u];            /* periodic wobble        */
    *y_mg = WOBBLE[(phase + 2u) % 8u];     /* phase-shifted wobble   */
    *z_mg = 1000;                          /* steady +1 g resting axis */
}

int fleet_build_frame(uint16_t seq, int32_t x_mg, int32_t y_mg, int32_t z_mg,
                      uint8_t *out, size_t cap)
{
    uint8_t payload[SK_PAYLOAD_LEN];
    int plen = sk_payload_pack(payload, sizeof payload, seq, x_mg, y_mg, z_mg);
    if (plen < 0) {
        return plen;                       /* propagate pack error */
    }
    return sk_frame_encode(payload, (size_t)plen, out, cap);
}
