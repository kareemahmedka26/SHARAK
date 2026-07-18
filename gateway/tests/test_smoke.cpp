/*
 * test_smoke.cpp — commit 1: prove the build works before any gateway logic.
 *
 * The real risk this catches is the C/C++ boundary: protocol.c is compiled by
 * the C compiler, this file by the C++ compiler. If the extern "C" guards in
 * protocol.h were missing, C++ would mangle the symbol name and the LINKER
 * would fail with "undefined reference" even though the code compiles.
 *
 * So this calls a C function from C++ and checks it against the published
 * CRC-16/CCITT-FALSE catalog vector: crc("123456789") == 0x29B1.
 */
#include <cstdio>
#include <cstdint>
#include <cstring>

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
    const char *msg = "123456789";
    const uint16_t crc =
        crc16_ccitt_false(reinterpret_cast<const uint8_t *>(msg),
                          std::strlen(msg));

    CHECK(crc == 0x29B1u);          /* the C seam works, end to end */

    std::printf("\ntest_smoke: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
