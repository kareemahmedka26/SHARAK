#ifndef SHARAK_SIM_FLEET_NODE_H
#define SHARAK_SIM_FLEET_NODE_H

/*
 * fleet_node.h — Sharak fleet node simulator (development scaffolding).
 *
 * A simulated vibration node that emits telemetry frames BYTE-EXACT with the
 * real firmware, because it encodes through the very same protocol.c
 * (sk_payload_pack -> sk_frame_encode). It is C, links protocol.c with C
 * linkage, and exists only to put a realistic multi-source load in front of
 * the gateway under test (REQ-SIM-001..005). It is NOT a product layer.
 *
 * The signal and frame-building logic live here (pure, testable); the TCP
 * server / CLI lives in fleet_main.c so this unit can be unit-tested with no
 * sockets.
 */

#include <stdint.h>
#include <stddef.h>

/*
 * Deterministic, integer-only synthetic vibration sample for a given phase
 * counter. No RNG, no floats, fully reproducible (REQ-SIM-001):
 *   seq  = phase (wraps as uint16)
 *   z_mg = +1000 (steady ~ +1 g, the resting axis)
 *   x_mg, y_mg = small periodic wobble from a fixed 8-entry table
 * Out-pointers must be non-NULL.
 */
void fleet_sample(uint32_t phase,
                  uint16_t *seq, int32_t *x_mg, int32_t *y_mg, int32_t *z_mg);

/*
 * Build one complete HDLC frame for the given reading into `out` (capacity
 * `cap`, >= SK_FRAME_MAX) by calling the real protocol encoder. Returns the
 * frame length, or a negative value on bad args / insufficient capacity
 * (propagated from protocol.c).
 */
int fleet_build_frame(uint16_t seq, int32_t x_mg, int32_t y_mg, int32_t z_mg,
                      uint8_t *out, size_t cap);

#endif /* SHARAK_SIM_FLEET_NODE_H */
