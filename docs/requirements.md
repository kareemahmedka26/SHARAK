# Sharak — Requirements (node firmware, protocol, build & CI)

Style: ASPICE SWE.1-flavored numbered requirements. Each requirement is
testable; the traceability table at the bottom maps every requirement to the
artifact that implements it and the test (or inspection) that verifies it.

Scope note: this document specifies the **delivered** layers — the toolchain /
build / CI spine, the node firmware, and the portable protocol. The Linux
gateway, kernel driver, and MQTT/cloud layers carry their own requirements and
are not covered here.

---

## 1. Node firmware (REQ-FW)

- **REQ-FW-001** The firmware SHALL target the TI Stellaris LM3S6965
  (ARM Cortex-M3, Thumb-2, no FPU) with 256 KB flash at `0x0000_0000` and
  64 KB SRAM at `0x2000_0000`, and SHALL run unmodified on QEMU's
  `lm3s6965evb` machine.
- **REQ-FW-002** The firmware SHALL provide its own vector table and reset
  handler that copies `.data` from flash to RAM and zero-fills `.bss` before
  calling `main()` (no vendor SDK, no CRT).
- **REQ-FW-003** The firmware SHALL NOT use dynamic memory allocation, and
  SHALL NOT use floating-point arithmetic (G-2); it SHALL link with
  `-nostdlib`.
- **REQ-FW-004** The firmware SHALL transmit telemetry over UART0 using
  polled (busy-wait) TX, and SHALL provide a polled, non-blocking RX read.
- **REQ-FW-005** All ADXL345 register access SHALL go through an abstract
  `i2c_bus_t` interface (function pointers + context), so the sensor driver
  is independent of the bus backend (dependency injection). Rationale: QEMU's
  `lm3s6965evb` does not emulate the I2C peripheral, so the same driver must
  run against a simulated backend in QEMU and the real I2C0 master on
  hardware.
- **REQ-FW-006** A simulated I2C backend (`i2c_sim`) SHALL emulate an ADXL345
  register file (DEVID = `0xE5`, POWER_CTL / DATA_FORMAT / BW_RATE writable,
  data registers `0x32..0x37`) and SHALL produce a deterministic, integer-only
  synthetic vibration signal (Z ≈ +1 g, small periodic wobble on X/Y).
- **REQ-FW-007** A real-hardware I2C backend (`i2c_stellaris`) SHALL implement
  an I2C0 single-master per the LM3S6965 datasheet (RCGC clock gating, PB2/PB3
  alternate function + open drain, I2CMCR/I2CMTPR setup, I2CMSA/I2CMCS/I2CMDR
  transfers). It SHALL compile in every firmware build even when unused.
- **REQ-FW-008** `adxl345_init()` SHALL verify DEVID == `0xE5` and SHALL fail
  with a distinct error code otherwise; it SHALL configure FULL_RES mode,
  ±16 g range (DATA_FORMAT = `0x0B`), 100 Hz output data rate
  (BW_RATE = `0x0A`), and then enable measurement (POWER_CTL = `0x08`).
- **REQ-FW-009** `adxl345_read()` SHALL read all six data registers
  (`0x32..0x37`) in a single burst (one write_read transaction), never
  register-by-register, to avoid torn samples (datasheet: "a multiple-byte
  read of all registers" is recommended).
- **REQ-FW-010** Acceleration SHALL be decoded as little-endian signed 16-bit
  two's complement and scaled to milli-g via `value_mg = raw * 125 / 32`
  (FULL_RES sensitivity 256 LSB/g), using multiply-before-divide in `int32_t`
  (G-1) and sign reconstruction via `(int32_t)(int16_t)` (G-7).
- **REQ-FW-011** All driver entry points SHALL check pointer arguments for
  NULL and SHALL report errors via a documented negative error-code enum
  (never by halting).
- **REQ-FW-012** The main loop SHALL sample, encode, and transmit one frame
  per iteration at a nominal 100 Hz cadence (crude busy-wait delay is
  acceptable; QEMU has no calibrated timing), using only static buffers.

## 2. Portable protocol (REQ-PR)

- **REQ-PR-001** The protocol library SHALL be pure, portable C (C17):
  no heap, no I/O, no globals, no libc beyond `<stdint.h>`/`<stddef.h>`;
  the same source SHALL compile for ARM bare-metal and the host.
- **REQ-PR-002** The library SHALL implement CRC-16/CCITT-FALSE: polynomial
  `0x1021`, init `0xFFFF`, no input/output reflection, xorout `0x0000`,
  bitwise (no lookup table). The CRC of ASCII `"123456789"` SHALL equal
  `0x29B1` (catalog check value).
- **REQ-PR-003** The telemetry payload SHALL be exactly 16 bytes:
  `ver(1)=0x01 | type(1)=0x01 | seq(2 LE) | x_mg(4 LE) | y_mg(4 LE) | z_mg(4 LE)`.
- **REQ-PR-004** The 16-bit CRC SHALL be computed over the 16-byte payload and
  appended **big-endian** (high byte first) before byte stuffing.
- **REQ-PR-005** Framing SHALL be HDLC-style: frames delimited by FLAG
  (`0x7E`); any `0x7E`/`0x7D` inside the frame SHALL be sent as `0x7D`
  followed by `byte ^ 0x20`.
- **REQ-PR-006** Frame decoding SHALL be an incremental state machine fed one
  byte at a time, able to resynchronize on the next FLAG after garbage, a bad
  escape, an over-long frame, or a CRC mismatch, reporting each failure with
  a distinct negative error code.
- **REQ-PR-007** All protocol APIs SHALL use caller-provided buffers with
  explicit `size_t` capacities and bounds checks, and SHALL be const-correct.

## 3. Build, run & CI (REQ-BU)

- **REQ-BU-001** A top-level `Makefile` SHALL provide targets `all`,
  `firmware`, `test`, `clean`. `firmware` SHALL cross-compile with
  `arm-none-eabi-gcc` (`-mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib`,
  linked with `firmware/lm3s6965.ld`), produce a raw `.bin` via objcopy, and
  print the image size. `test` SHALL build every `tests/test_*.c` with host
  `gcc -Wall -Wextra` and fail on any nonzero exit. `all` SHALL run both.
- **REQ-BU-002** `scripts/run_qemu.sh` SHALL boot the firmware with
  `qemu-system-arm -M lm3s6965evb -nographic` and expose UART0 on
  `tcp:127.0.0.1:5555` (server, nowait).
- **REQ-BU-003** A GitHub Actions workflow on `ubuntu-latest` SHALL install
  `gcc-arm-none-eabi` and run `make all`; the workflow SHALL fail if any test
  fails or the firmware does not build warning-free (`-Werror`).
- **REQ-BU-004** The build SHALL complete with zero warnings on Ubuntu with
  gcc and arm-none-eabi-gcc.

## 4. Verification & tests (REQ-TS)

- **REQ-TS-001** Every pure (hardware-independent) function SHALL have a host
  unit test that exits nonzero on failure (G-8): decode, CRC, framing, the
  i2c_sim backend, and the ADXL345 driver against the sim.
- **REQ-TS-002** Test vectors SHALL be derived from the datasheet/spec math
  and documented in test comments — never taken from the implementation's own
  output (G-9).
- **REQ-TS-003** The previously reviewed `adxl345_decode()` contract and its
  unit tests SHALL be preserved: 6 raw bytes →
  `(int32_t)(int16_t)(b0 | b1<<8) * 125 / 32` per axis.

---

## Traceability

| Requirement | Implemented in | Verified by |
|---|---|---|
| REQ-FW-001 | `firmware/lm3s6965.ld`, `Makefile` | QEMU boot (`scripts/run_qemu.sh`), CI cross-build |
| REQ-FW-002 | `firmware/startup.c`, `firmware/lm3s6965.ld` | QEMU boot; inspection (`.map` file) |
| REQ-FW-003 | all of `firmware/` | inspection; link with `-nostdlib` succeeds (no malloc/float refs) |
| REQ-FW-004 | `firmware/uart.c` | QEMU serial output visible on tcp:5555 |
| REQ-FW-005 | `firmware/i2c.h` | `tests/test_adxl345_driver.c` (driver runs on a fake bus) |
| REQ-FW-006 | `firmware/i2c_sim.c` | `tests/test_i2c_sim.c` |
| REQ-FW-007 | `firmware/i2c_stellaris.c` | compiles in every firmware build (CI); datasheet-ref inspection |
| REQ-FW-008 | `firmware/adxl345.c` (`adxl345_init`) | `tests/test_adxl345_driver.c` |
| REQ-FW-009 | `firmware/adxl345.c` (`adxl345_read`) | `tests/test_adxl345_driver.c`, `tests/test_i2c_sim.c` |
| REQ-FW-010 | `firmware/adxl345.c` (`adxl345_decode`) | `tests/test_adxl345_decode.c` |
| REQ-FW-011 | `firmware/adxl345.c`, `firmware/i2c_sim.c` | `tests/test_adxl345_driver.c` (NULL/error-path cases) |
| REQ-FW-012 | `firmware/main.c` | QEMU run: continuous frames on tcp:5555 |
| REQ-PR-001 | `protocol/protocol.c`/`.h` | compiles for both targets (CI); inspection |
| REQ-PR-002 | `protocol/protocol.c` (`crc16_ccitt_false`) | `tests/test_crc16.c` |
| REQ-PR-003 | `protocol/protocol.c` (payload pack/unpack) | `tests/test_framing.c` |
| REQ-PR-004 | `protocol/protocol.c` (frame encode) | `tests/test_framing.c`, `tests/test_crc16.c` (residue) |
| REQ-PR-005 | `protocol/protocol.c` (stuffing) | `tests/test_framing.c` |
| REQ-PR-006 | `protocol/protocol.c` (decoder state machine) | `tests/test_framing.c` (partial feed, corruption, resync) |
| REQ-PR-007 | `protocol/protocol.h` API | inspection + bounds tests in `tests/test_framing.c` |
| REQ-BU-001 | `Makefile` | CI `make all`; local run |
| REQ-BU-002 | `scripts/run_qemu.sh` | manual: `nc 127.0.0.1 5555 \| xxd \| head` |
| REQ-BU-003 | `.github/workflows/ci.yml` | CI run on push |
| REQ-BU-004 | `-Werror` in `Makefile` | CI build log |
| REQ-TS-001 | `tests/` | `make test` |
| REQ-TS-002 | `tests/` comments | review/inspection |
| REQ-TS-003 | `firmware/adxl345.c`, `tests/test_adxl345_decode.c` | `make test` |
