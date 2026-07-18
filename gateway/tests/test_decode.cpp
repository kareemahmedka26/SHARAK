/*
 * test_decode.cpp — the decode seam.
 *
 * Payloads are built with the production packers from protocol.c, so this test
 * exercises the real wire bytes rather than hand-written literals. Framing and
 * CRC are already proven elsewhere; here we only care that a valid payload
 * becomes the right typed value, and that bad input is rejected without
 * touching the caller's Telemetry.
 */
#include <cstdio>
#include <cstdint>

#include <sharak/decode.hpp>
#include <sharak/protocol.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            std::printf("%s:%d: CHECK failed: %s\n",                         \
                        __FILE__, __LINE__, #cond);                          \
        }                                                                    \
    } while (0)

int main()
{
    using namespace sharak;

    /* ---- 1. accel payload decodes to the right kind and values ----------- */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        const int n = sk_payload_pack(buf, sizeof buf, 0x1234u, 1000, -2000, 256);
        CHECK(n == 16);

        CHECK(decode(buf, static_cast<std::size_t>(n), t) == DecodeStatus::Ok);
        CHECK(t.kind == Kind::Accel);
        CHECK(t.seq  == 0x1234u);
        CHECK(t.x_mg == 1000);
        CHECK(t.y_mg == -2000);
        CHECK(t.z_mg == 256);
    }

    /* ---- 2. temperature ------------------------------------------------- */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        const int n = sk_pack_temp(buf, sizeof buf, 0x0042u, -1725);
        CHECK(n == 6);

        CHECK(decode(buf, static_cast<std::size_t>(n), t) == DecodeStatus::Ok);
        CHECK(t.kind         == Kind::Temp);
        CHECK(t.seq          == 0x0042u);
        CHECK(t.temp_centi_c == -1725);
    }

    /* ---- 3. current ------------------------------------------------------ */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        const int n = sk_pack_current(buf, sizeof buf, 0xABCDu, -125000);
        CHECK(n == 8);

        CHECK(decode(buf, static_cast<std::size_t>(n), t) == DecodeStatus::Ok);
        CHECK(t.kind       == Kind::Current);
        CHECK(t.seq        == 0xABCDu);
        CHECK(t.current_ma == -125000);
    }

    /* ---- 4. too short to hold an envelope is rejected -------------------- */
    {
        const uint8_t buf[2] = { SK_VER, SK_TYPE_TEMP };
        Telemetry t;
        CHECK(decode(buf, sizeof buf, t) == DecodeStatus::BadLength);
    }

    /* ---- 5. a temp body truncated by one byte is rejected ---------------- */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        (void)sk_pack_temp(buf, sizeof buf, 1u, 2500);
        CHECK(decode(buf, 5u, t) == DecodeStatus::BadLength);   /* needs 6 */
    }

    /* ---- 6. unknown type is reported, not guessed ------------------------ */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        (void)sk_pack_temp(buf, sizeof buf, 1u, 2500);
        buf[1] = 0x7Fu;                                  /* no such type */
        CHECK(decode(buf, 6u, t) == DecodeStatus::UnknownType);
    }

    /* ---- 7. wrong protocol version is rejected --------------------------- */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        (void)sk_pack_temp(buf, sizeof buf, 1u, 2500);
        buf[0] = 0x99u;                                  /* not SK_VER */
        CHECK(decode(buf, 6u, t) == DecodeStatus::BadVersion);
    }

    /* ---- 8. on failure the caller's Telemetry is left untouched ---------- */
    {
        uint8_t buf[SK_PAYLOAD_MAX];
        Telemetry t;
        t.seq = 0xBEEFu;                                 /* sentinel */
        (void)sk_pack_temp(buf, sizeof buf, 7u, 2500);
        buf[0] = 0x99u;                                  /* force BadVersion */
        CHECK(decode(buf, 6u, t) == DecodeStatus::BadVersion);
        CHECK(t.seq == 0xBEEFu);                         /* must be unchanged */
    }

    std::printf("\ntest_decode: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
