#ifndef SHARAK_PROTOCOL_H
#define SHARAK_PROTOCOL_H

/*
 * protocol.h — Sharak wire protocol: CRC-16, fixed payload, HDLC framing.
 *
 * Purpose
 *   The single source of truth for the bytes on the wire. The SAME protocol.c
 *   is compiled into the bare-metal node AND (later) the Linux gateway, so the
 *   two ends can never disagree about the format (docs/architecture.md).
 *
 * Constraints (REQ-PR-001/007)
 *   Pure, portable C17: no heap, no I/O, no globals, no libc beyond
 *   <stdint.h>/<stddef.h>. Every function takes caller-provided buffers with
 *   explicit size_t capacities, bounds-checks them, and is const-correct.
 *
 * Wire format
 *   Payload (16 bytes, fixed):
 *     ver(1)=0x01 | type(1)=0x01 | seq(2 LE) | x_mg(4 LE) | y_mg(4 LE) | z_mg(4 LE)
 *   Frame:
 *     0x7E  stuffed( payload[16] + crc_hi + crc_lo )  0x7E
 *   CRC-16/CCITT-FALSE over the 16 payload bytes, appended BIG-ENDIAN.
 *   Byte stuffing: 0x7E -> 0x7D 0x5E, 0x7D -> 0x7D 0x5D (escaped = byte ^ 0x20).
 */

#include <stdint.h>
#include <stddef.h>

/* ---- sizes & constants ----------------------------------------------------*/
#define SK_PAYLOAD_LEN   16u            /* fixed payload size                  */
#define SK_CRC_LEN        2u            /* CRC-16 appended after payload       */
#define SK_FRAME_INNER   (SK_PAYLOAD_LEN + SK_CRC_LEN)   /* 18 bytes pre-stuff */
/* Worst case: every inner byte stuffed (x2) + 2 FLAG bytes = 38. */
#define SK_FRAME_MAX     (2u * SK_FRAME_INNER + 2u)      /* 38 bytes           */

#define SK_FLAG          0x7Eu          /* frame delimiter                     */
#define SK_ESC           0x7Du          /* escape byte                         */
#define SK_ESC_XOR       0x20u          /* escaped value = byte ^ 0x20         */

#define SK_VER           0x01u          /* protocol version                    */
#define SK_TYPE_ACCEL    0x01u          /* accelerometer sample message        */

/* ---- frame decode (incremental state machine) -----------------------------*/
/* Per-byte feed results / errors (REQ-PR-006). */
#define SK_DEC_MORE        0    /* byte consumed, no frame yet                 */
#define SK_DEC_FRAME      16    /* a complete, CRC-valid 16-byte payload is out */
#define SK_DEC_ERR_LEN    (-1)  /* frame ended with wrong length               */
#define SK_DEC_ERR_CRC    (-2)  /* length ok but CRC mismatched                */
#define SK_DEC_ERR_OFLOW  (-3)  /* frame grew past the maximum inner length    */

typedef struct {
    uint8_t  in_frame;                  /* 0 = hunting for FLAG, 1 = collecting */
    uint8_t  esc;                       /* previous byte was ESC                */
    uint8_t  overflow;                  /* too many bytes since last FLAG       */
    uint16_t count;                     /* inner bytes collected so far         */
    uint8_t  buf[SK_FRAME_INNER];       /* un-stuffed payload+CRC accumulator   */
} sk_decoder_t;


/*----------------- function prototypes --------------------------------------*/

/* when compiling c++ */
#ifdef __cplusplus
extern "C"
{
#endif 

/* ---- CRC-16/CCITT-FALSE ---------------------------------------------------*/
/*
 * Poly 0x1021, init 0xFFFF, no input/output reflection, xorout 0x0000.
 * Bitwise (no table). Catalog check: crc16_ccitt_false("123456789") == 0x29B1.
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

/* ---- payload pack / unpack ------------------------------------------------*/
/*
 * Assemble the 16-byte payload into `buf` (capacity `cap`). All multi-byte
 * fields are little-endian, built by shift/OR (G-3), never by struct casting.
 * Returns the number of bytes written (16) or a negative value if cap < 16.
 */
int sk_payload_pack(uint8_t *buf, size_t cap,
                    uint16_t seq, int32_t x_mg, int32_t y_mg, int32_t z_mg);

/*
 * Parse a 16-byte payload. Any out-pointer may be NULL (field skipped).
 * Returns 0 on success, negative if len < 16 or buf is NULL.
 */
int sk_payload_unpack(const uint8_t *buf, size_t len,
                      uint8_t *ver, uint8_t *type, uint16_t *seq,
                      int32_t *x_mg, int32_t *y_mg, int32_t *z_mg);

/* ---- frame encode ---------------------------------------------------------*/
/*
 * Encode a 16-byte payload into a complete HDLC frame in `out` (capacity
 * `cap`, should be >= SK_FRAME_MAX). Computes the CRC, appends it big-endian,
 * byte-stuffs, and wraps with FLAG bytes.
 * Returns the frame length, or negative on bad args / insufficient capacity.
 */
int sk_frame_encode(const uint8_t *payload, size_t plen,
                    uint8_t *out, size_t cap);

/*
 * Feed one received byte. On a completed good frame, copies the 16-byte
 * payload into `out` (capacity `cap`) and returns SK_DEC_FRAME. Returns
 * SK_DEC_MORE while collecting/hunting, or a negative SK_DEC_ERR_* at the
 * closing FLAG of a bad frame. The decoder always resynchronizes on the next
 * FLAG regardless of the error.
 */
int sk_decoder_feed(sk_decoder_t *st, uint8_t byte, uint8_t *out, size_t cap);

/* Reset a decoder to the hunting state. */
void sk_decoder_init(sk_decoder_t *st);

/* when compiling c++ */
#ifdef __cplusplus
}
#endif

#endif /* SHARAK_PROTOCOL_H */
