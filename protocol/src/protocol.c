/*
 * protocol.c — Sharak wire protocol implementation (pure, portable C17).
 *
 * Contains: CRC-16/CCITT-FALSE (bitwise), the fixed 16-byte payload pack/unpack,
 * HDLC-style frame encode, and an incremental byte-at-a-time frame decoder.
 * No heap, no I/O, no globals (REQ-PR-001) — the identical object compiles into
 * the ARM firmware and (later) the gateway, guaranteeing one wire format.
 *
 * All multi-byte (de)serialization is explicit shift/OR (G-3): endianness is
 * visible in the code and there is no reliance on the host's byte order or on
 * casting buffer pointers to wider integer types (which would be UB).
 */
#include <sharak/protocol.h>

/* ===========================================================================
 * CRC-16/CCITT-FALSE
 *   width=16 poly=0x1021 init=0xFFFF refin=false refout=false xorout=0x0000
 * Bitwise (no lookup table): processes the message MSB-first, shifting the
 * augmented register and XORing the polynomial whenever the top bit is set.
 * =========================================================================== */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == NULL) {
        return crc;                 /* defined as the init value (no bytes) */
    }

    for (size_t i = 0u; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);   /* bring byte into the high half */
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ===========================================================================
 * Payload pack / unpack (16 bytes, little-endian fields)
 * =========================================================================== */
int sk_payload_pack(uint8_t *buf, size_t cap,
                    uint16_t seq, int32_t x_mg, int32_t y_mg, int32_t z_mg)
{
    if (buf == NULL || cap < SK_PAYLOAD_LEN) {
        return -1;
    }

    /* signed->unsigned conversion is well defined (modulo 2^32) and gives the
     * exact two's-complement bytes we want on the wire. */
    uint32_t ux = (uint32_t)x_mg;
    uint32_t uy = (uint32_t)y_mg;
    uint32_t uz = (uint32_t)z_mg;

    buf[0] = SK_VER;
    buf[1] = SK_TYPE_ACCEL;
    buf[2] = (uint8_t)(seq & 0xFFu);
    buf[3] = (uint8_t)((seq >> 8) & 0xFFu);

    buf[4] = (uint8_t)(ux & 0xFFu);
    buf[5] = (uint8_t)((ux >> 8) & 0xFFu);
    buf[6] = (uint8_t)((ux >> 16) & 0xFFu);
    buf[7] = (uint8_t)((ux >> 24) & 0xFFu);

    buf[8]  = (uint8_t)(uy & 0xFFu);
    buf[9]  = (uint8_t)((uy >> 8) & 0xFFu);
    buf[10] = (uint8_t)((uy >> 16) & 0xFFu);
    buf[11] = (uint8_t)((uy >> 24) & 0xFFu);

    buf[12] = (uint8_t)(uz & 0xFFu);
    buf[13] = (uint8_t)((uz >> 8) & 0xFFu);
    buf[14] = (uint8_t)((uz >> 16) & 0xFFu);
    buf[15] = (uint8_t)((uz >> 24) & 0xFFu);

    return (int)SK_PAYLOAD_LEN;
}

int sk_payload_unpack(const uint8_t *buf, size_t len,
                      uint8_t *ver, uint8_t *type, uint16_t *seq,
                      int32_t *x_mg, int32_t *y_mg, int32_t *z_mg)
{
    if (buf == NULL || len < SK_PAYLOAD_LEN) {
        return -1;
    }

    if (ver != NULL) {
        *ver = buf[0];
    }
    if (type != NULL) {
        *type = buf[1];
    }
    if (seq != NULL) {
        *seq = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    }
    /* Reassemble LE bytes into uint32, then cast to int32_t to recover the
     * signed value (the documented two's-complement reinterpretation, G-3). */
    if (x_mg != NULL) {
        *x_mg = (int32_t)((uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                          ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24));
    }
    if (y_mg != NULL) {
        *y_mg = (int32_t)((uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                          ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24));
    }
    if (z_mg != NULL) {
        *z_mg = (int32_t)((uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                          ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24));
    }
    return 0;
}

/* ===========================================================================
 * Other message types (0x02 temp, 0x03 current) — share the 4-byte envelope
 * =========================================================================== */

/* Write ver, type, seq into the first 4 bytes. Caller guarantees cap >= 4. */
static void put_envelope(uint8_t *buf, uint8_t type, uint16_t seq)
{
    buf[0] = SK_VER;
    buf[1] = type;
    buf[2] = (uint8_t)(seq & 0xFFu);
    buf[3] = (uint8_t)((seq >> 8) & 0xFFu);
}

int sk_pack_temp(uint8_t *buf, size_t cap, uint16_t seq, int16_t temp_centi_c)
{
    uint16_t ut;

    if (buf == NULL || cap < SK_PAYLOAD_MIN) {
        return -1;
    }
    put_envelope(buf, SK_TYPE_TEMP, seq);
    ut = (uint16_t)temp_centi_c;               /* two's-complement bytes on wire */
    buf[4] = (uint8_t)(ut & 0xFFu);
    buf[5] = (uint8_t)((ut >> 8) & 0xFFu);
    return 6;                                   /* SK_ENVELOPE_LEN + 2            */
}

int sk_pack_current(uint8_t *buf, size_t cap, uint16_t seq, int32_t current_ma)
{
    uint32_t uc;

    if (buf == NULL || cap < 8u) {
        return -1;
    }
    put_envelope(buf, SK_TYPE_CURRENT, seq);
    uc = (uint32_t)current_ma;
    buf[4] = (uint8_t)(uc & 0xFFu);
    buf[5] = (uint8_t)((uc >> 8) & 0xFFu);
    buf[6] = (uint8_t)((uc >> 16) & 0xFFu);
    buf[7] = (uint8_t)((uc >> 24) & 0xFFu);
    return 8;                                   /* SK_ENVELOPE_LEN + 4            */
}

int sk_envelope_unpack(const uint8_t *buf, size_t len,
                       uint8_t *ver, uint8_t *type, uint16_t *seq)
{
    if (buf == NULL || len < SK_ENVELOPE_LEN) {
        return -1;
    }
    if (ver != NULL) {
        *ver = buf[0];
    }
    if (type != NULL) {
        *type = buf[1];
    }
    if (seq != NULL) {
        *seq = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    }
    return 0;
}

int sk_unpack_temp(const uint8_t *buf, size_t len, int16_t *temp_centi_c)
{
    if (buf == NULL || len < 6u) {
        return -1;
    }
    if (temp_centi_c != NULL) {
        *temp_centi_c = (int16_t)((uint16_t)((uint16_t)buf[4] |
                                             ((uint16_t)buf[5] << 8)));
    }
    return 0;
}

int sk_unpack_current(const uint8_t *buf, size_t len, int32_t *current_ma)
{
    if (buf == NULL || len < 8u) {
        return -1;
    }
    if (current_ma != NULL) {
        *current_ma = (int32_t)((uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                                ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24));
    }
    return 0;
}

/* ===========================================================================
 * Frame encode: CRC (big-endian) + HDLC byte stuffing between FLAG bytes
 * =========================================================================== */
int sk_frame_encode(const uint8_t *payload, size_t plen,
                    uint8_t *out, size_t cap)
{
    uint8_t inner[SK_FRAME_INNER];
    uint16_t crc;
    size_t n = 0u;
    size_t inner_len;

    if (payload == NULL || out == NULL ||
        plen < SK_PAYLOAD_MIN || plen > SK_PAYLOAD_MAX) {
        return -1;
    }

    /* inner = payload[plen] followed by CRC, high byte first (big-endian). The
     * one big-endian item on an otherwise LE wire — it makes the receiver's
     * residue check (CRC over payload+crc == 0) work without byte swapping. */
    for (size_t i = 0u; i < plen; i++) {
        inner[i] = payload[i];
    }
    crc = crc16_ccitt_false(payload, plen);
    inner[plen]      = (uint8_t)((crc >> 8) & 0xFFu);  /* hi */
    inner[plen + 1u] = (uint8_t)(crc & 0xFFu);         /* lo */
    inner_len = plen + SK_CRC_LEN;

    /* opening FLAG */
    if (cap < 1u) {
        return -1;
    }
    out[n++] = SK_FLAG;

    /* stuff each inner byte */
    for (size_t i = 0u; i < inner_len; i++) {
        uint8_t b = inner[i];
        if (b == SK_FLAG || b == SK_ESC) {
            if (n + 2u > cap) {
                return -1;
            }
            out[n++] = SK_ESC;
            out[n++] = (uint8_t)(b ^ SK_ESC_XOR);
        } else {
            if (n + 1u > cap) {
                return -1;
            }
            out[n++] = b;
        }
    }

    /* closing FLAG */
    if (n + 1u > cap) {
        return -1;
    }
    out[n++] = SK_FLAG;

    return (int)n;
}

/* ===========================================================================
 * Frame decode: incremental, byte-at-a-time state machine
 * =========================================================================== */
void sk_decoder_init(sk_decoder_t *st)
{
    if (st == NULL) {
        return;
    }
    st->in_frame = 0u;
    st->esc      = 0u;
    st->overflow = 0u;
    st->count    = 0u;
}

/* Validate a just-closed frame and, if good, deliver the payload. */
static int decoder_finish(sk_decoder_t *st, uint8_t *out, size_t cap)
{
    size_t   payload_len;
    uint16_t crc;
    uint16_t recv;

    if (st->overflow || st->count > SK_FRAME_INNER) {
        return SK_DEC_ERR_OFLOW;
    }
    /* Length is carried by the frame itself: inner = payload + 2-byte CRC, so a
     * valid frame has at least SK_PAYLOAD_MIN + SK_CRC_LEN inner bytes. */
    if (st->count < (uint16_t)(SK_PAYLOAD_MIN + SK_CRC_LEN)) {
        return SK_DEC_ERR_LEN;
    }
    payload_len = (size_t)st->count - SK_CRC_LEN;

    crc  = crc16_ccitt_false(st->buf, payload_len);
    recv = (uint16_t)(((uint16_t)st->buf[payload_len] << 8) |
                       (uint16_t)st->buf[payload_len + 1u]);   /* big-endian */
    if (crc != recv) {
        return SK_DEC_ERR_CRC;
    }

    if (out == NULL || cap < payload_len) {
        return SK_DEC_ERR_LEN;          /* nowhere to deliver; treat as reject */
    }
    for (size_t i = 0u; i < payload_len; i++) {
        out[i] = st->buf[i];
    }
    return (int)payload_len;            /* positive = complete frame, N bytes   */
}

int sk_decoder_feed(sk_decoder_t *st, uint8_t byte, uint8_t *out, size_t cap)
{
    if (st == NULL) {
        return SK_DEC_ERR_LEN;
    }

    if (byte == SK_FLAG) {
        int result = SK_DEC_MORE;
        /* A FLAG closes any frame in progress (validate it), then opens a new
         * one — that is how a single 0x7E both ends and begins frames. */
        if (st->in_frame && st->count > 0u) {
            result = decoder_finish(st, out, cap);
        }
        st->in_frame = 1u;
        st->esc      = 0u;
        st->overflow = 0u;
        st->count    = 0u;
        return result;
    }

    if (!st->in_frame) {
        return SK_DEC_MORE;             /* garbage between frames: ignore */
    }

    if (byte == SK_ESC) {
        st->esc = 1u;                   /* next byte is escaped */
        return SK_DEC_MORE;
    }

    {
        uint8_t b = byte;
        if (st->esc) {
            b = (uint8_t)(byte ^ SK_ESC_XOR);
            st->esc = 0u;
        }
        if (st->count < SK_FRAME_INNER) {
            st->buf[st->count++] = b;
        } else {
            st->overflow = 1u;          /* too long; reported at closing FLAG */
        }
    }
    return SK_DEC_MORE;
}
