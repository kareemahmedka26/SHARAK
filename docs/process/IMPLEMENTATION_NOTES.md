# Sharak — Implementation Notes (study companion)

This file is the per-file walkthrough for studying the Sprint-1 code:
what each file does, the decisions inside it, what to pay attention to, and
which C concepts it exercises. It is filled in milestone by milestone as the
modules land — each section is added in the same commit as its code.

Status legend: ✅ written · 🚧 lands in a later milestone commit

| File | Status |
|---|---|
| `Makefile` | ✅ |
| `firmware/lm3s6965.ld`, `firmware/startup.c` | ✅ |
| `firmware/uart.c/.h` | ✅ |
| `firmware/i2c.h`, `firmware/i2c_sim.c`, `firmware/i2c_stellaris.c` | ✅ |
| `firmware/adxl345.c/.h` | ✅ |
| `protocol/protocol.c/.h` | ✅ |
| `firmware/main.c`, `scripts/run_qemu.sh` | ✅ |
| `.github/workflows/ci.yml` | ✅ |
| Environment limitations (what was / wasn't verified here) | ✅ |

---

## Makefile

**What it does.** One entry point for everything: `make firmware` cross-builds
the node image, `make test` builds and runs every host test, `make all` does
both (and is exactly what CI runs).

**Key decisions.**
- *Two compilers, one tree.* The portable files (`protocol.c`, `adxl345.c`,
  `i2c_sim.c`) are compiled twice — by `arm-none-eabi-gcc` into the image and
  by host `gcc` into the tests. If a file sneaks in a libc call or
  undefined behavior that only one compiler tolerates, the build breaks
  immediately. The build system is acting as a portability test.
- *Explicit `FW_SRCS` list, wildcard tests.* Firmware contents are
  security/size-relevant, so nothing enters the image without being listed.
  Tests are the opposite: `$(wildcard tests/test_*.c)` means adding a test
  file is enough to make CI run it — no Makefile edit, nothing to forget.
- *`-Werror` everywhere.* The Definition of Done requires a warning-free
  build; enforcing it in the compiler beats enforcing it in code review.
- *`|| exit 1` in the test loop.* Each test binary signals failure with a
  nonzero exit status (guideline G-8); the loop propagates the first failure
  so `make test` — and therefore CI — goes red.

**Study notes / C-adjacent concepts.**
- `?=` vs `:=` vs `=` in Make (overridable defaults vs immediate expansion).
- Why `-ffreestanding` (C17 §4p6 — a *freestanding implementation* only has
  to provide headers like `<stdint.h>`, no library, and `main()` is not
  special) and how that pairs with `-nostdlib` at link time.
- Read `build/sharak_node.map` once after a build: find where `.data`'s
  load address (flash) differs from its run address (RAM) — that gap is the
  exact reason `startup.c` has a copy loop.

**Later addition — `-fno-tree-loop-distribute-patterns`.** At `-O2`, GCC's
loop-idiom recognizer can notice that the hand-written `.data` copy loop *is*
`memcpy` and the `.bss` zero loop *is* `memset`, and "optimize" them into CALLS
to those libc symbols. Under `-nostdlib` those symbols are not linked, so the
firmware link fails with `undefined reference to memcpy`. The whole reason
those loops are hand-written is to need no libc, so the flag tells the
optimizer not to undo that. (Protocol/byte loops are at risk too.) This is the
one Makefile change made beyond the skeleton, and it is firmware-scope only —
the host build is unaffected.

---

## firmware/lm3s6965.ld + firmware/startup.c

**What they do.** Together they ARE the C runtime for the board — there is no
crt0 because we link `-nostdlib`. The linker script lays out memory; the
startup file provides the vector table and the reset path that makes `main()`
callable.

**Key decisions.**
- *Two memory regions.* `FLASH` at `0x00000000` (256 KB) and `RAM` at
  `0x20000000` (64 KB) match the LM3S6965 map. Code/read-only data live in
  flash; the stack and mutable data live in RAM.
- *LMA vs VMA — the heart of the lesson.* Initialized globals (`.data`) must
  be *readable at runtime from RAM* (their VMA) but *stored in flash* (their
  LMA), because RAM is volatile and has no contents at power-on. The script
  places `.data` with `> RAM AT > FLASH`, which sets VMA=RAM, LMA=FLASH and
  exports `_sidata/_sdata/_edata` so the startup copy loop knows the three
  addresses. `.bss` has no initializer, so it only needs zeroing in RAM.
- *Vector table layout is an ABI, not a choice.* Cortex-M fetches **word[0] as
  the initial MSP** and **word[1] as the initial PC** straight out of address
  0. So `g_vectors[0] = _estack` and `g_vectors[1] = Reset_Handler`. Get the
  order wrong and the core faults before your first instruction.
- *Full-descending stack.* `_estack` is the TOP of RAM; the stack grows down.
  That is why the initial SP is the high address, not `_sdata`.

**Study notes.**
- Trace the reset path by hand: power-on → MSP=`_estack`, PC=`Reset_Handler`
  → copy `[_sidata.._sidata+(_edata-_sdata))` into `[_sdata.._edata)` → zero
  `[_sbss.._ebss)` → `main()`. If `main` ever returns, `Default_Handler`'s
  infinite loop catches it.
- `volatile` is deliberately absent from the copy/zero loops *in source* but
  the data they touch is real RAM; the optimizer note above (`-fno-tree-loop-
  distribute-patterns`) is what keeps those loops as loops.

---

## firmware/uart.c + firmware/uart.h

**What they do.** A tiny polled UART0 driver: bring the peripheral up, push
bytes out, and offer a *non-blocking* receive. This is the node's only window
to the world, and the only peripheral QEMU's `lm3s6965evb` actually emulates.

**Key decisions.**
- *Trimmed API.* The header exposes exactly `uart_init`, `uart_putc`,
  `uart_puts`, `uart_write`, `uart_try_getc`. The earlier scaffold's
  `put_uint/put_int/put_hex8` were removed: the node speaks a **binary**
  protocol now, so number-to-ASCII helpers are dead weight (and would tempt
  someone to log instead of frame).
- *Real init sequence (REQ-FW-002).* Enable the UART0 clock in `RCGC1`, disable
  the UART (`CTL=0`) before touching the baud divisors, program `IBRD/FBRD/LCRH`
  for 8N1 + FIFO, then re-enable `CTL` with `UARTEN|TXE|RXE`. Touching baud
  registers while enabled is the classic silent-misconfig bug; the disable
  dance avoids it.
- *Polled, non-blocking RX (REQ-FW-004).* `uart_try_getc` returns `-1` when
  `FR.RXFE` (RX FIFO empty) is set, otherwise the byte. No interrupts, no
  blocking — the super-loop stays in control. (The node doesn't consume RX yet,
  but the seam is there for a future command channel.)
- *TX is busy-wait on `FR.TXFF`.* Simple and correct for a watch-the-stream
  demo. A product would use the TX FIFO/IRQ; that's a named later step.

**Study notes.**
- QEMU ignores the actual baud divisors — it just moves bytes — so the
  IBRD/FBRD math (115200 @ ~12 MHz IOSC) is "correct for silicon, irrelevant to
  the emulator." The comment in the code says so to prevent a confused "why
  does any baud work?" moment.
- Memory-mapped registers are reached through `volatile` pointers so the
  compiler never caches or reorders the device writes.

---

## firmware/i2c.h + firmware/i2c_sim.c + firmware/i2c_stellaris.c

**What they do.** The dependency-injection seam that lets the *same* sensor
driver run two ways: against a software fake under QEMU, and against real I2C0
hardware on silicon.

**Why DI at all — the QEMU constraint.** The `lm3s6965evb` machine emulates the
UART but **not** I2C. If the ADXL345 driver called I2C registers directly, it
could never run under emulation. So `i2c_bus_t` is a struct of two function
pointers (`write`, `write_read`) plus a `void *ctx`; the driver talks only to
that interface. Swap the backend, keep the driver. This is the single most
important design decision in the firmware (see `../02_architecture_SWE2/architecture.md`).

**Key decisions.**
- *`i2c_sim` models behavior, not registers-for-their-own-sake.* It holds a
  register file, returns `0xE5` for `DEVID`, NACKs any address other than
  `0x53`, and — crucially — keeps the data registers **frozen until POWER_CTL
  Measure (bit 3) is set**, so the driver's init order is actually tested. Each
  6-byte burst takes an **atomic snapshot** so a sample can never be torn.
- *Deterministic synthetic motion, integer-only (G-2).* Z is a steady `+256`
  LSB (= +1 g = 1000 mg), X/Y read from a fixed 16-entry `wobble[]` table
  indexed by a phase counter that advances once per data-touching burst. No
  RNG, no floats — every test vector is reproducible by hand.
- *`i2c_stellaris` always compiles (REQ-FW-007) but cannot run under QEMU.* It
  is the real I2C0 master (GPIOB PB2/PB3, `I2CMCS` run/start/stop state
  machine). Keeping it in the build guards against bit-rot even though
  `--gc-sections` drops it from the QEMU image because nothing references it.

**The `I2CMCR` erratum (worth remembering).** On this part the master-enable bit
`MFE` is **bit 4 (0x10)**, and **bit 5 (0x20) is `SFE`, the *slave* enable**.
A natural "set 0x20 to turn the master on" reads plausibly and is wrong — it
would enable the slave engine and leave the master off. The code sets `0x10`
and the comment flags the trap.

**Study notes.**
- Function pointers as an interface = C's version of a vtable. `ctx` is the
  "this" pointer. Reading `i2c_sim_bind` shows how the concrete object is
  hidden behind the abstract `i2c_bus_t`.
- `test_i2c_sim.c` pokes `sim.regs[...]` directly — the struct is intentionally
  non-opaque *for the test*, which is a pragmatic trade (white-box test of a
  fake) you wouldn't make for production code.

---

## firmware/adxl345.c + firmware/adxl345.h

**What they do.** The accelerometer driver, split into a **pure** decode/scale
function (host-testable, no hardware) and **bus-dependent** init/read/selftest
that go through `i2c_bus_t`.

**The bug this milestone fixes (the headline story).** The preserved
`adxl345_decode` assembled each axis as `uint16_t` and then cast `(int32_t)x`.
For a negative reading like `0xFFFF` (which is −1 LSB) that produced
`+65535 → +255996 mg` instead of `≈ −4 mg`: the sign was thrown away. The fix
is one cast per axis: `(int32_t)(int16_t)x`. Reinterpreting the 16 bits as
*signed* first (C17 §6.3.1.3p3, implementation-defined but universally two's-
complement on our targets) and *then* widening preserves the sign. The
multiply-before-divide order (`* 125 / 32`, G-1) keeps resolution without
floats (G-2).

**Key decisions.**
- *Config bytes are named, not magic.* `DATA_FORMAT = FULL_RES | RANGE_16G =
  0x0B`, `BW_RATE = 0x0A` (100 Hz), `POWER_CTL = 0x08` (Measure). FULL_RES means
  constant 256 LSB/g on every range, which is *why* one scale constant works.
- *One 6-byte burst (REQ-FW-009).* `read` does a single `write_read` from
  `DATAX0` (0x32) for all six bytes, so X/Y/Z come from one instant — no torn
  sample across three transactions.
- *Defensive contract.* NULL args → `E_PARAM`; wrong `DEVID` → `E_WRONG_DEVID`;
  any backend failure → `E_BUS`. The status enum makes the failure mode legible
  at the call site instead of a bare `-1`.

**Study notes.**
- Two tests, two layers: `test_adxl345_decode.c` is the pure-math table (the
  bug's regression guard), `test_adxl345_driver.c` runs the *real* driver
  against `i2c_sim` and even checks the exact config bytes landed in the fake's
  register file. The `fail_write/fail_write_read` stub bus proves the `E_BUS`
  path.
- Why `z_mg == 1000` exactly: `256 * 125 / 32 = 1000` with integer math, no
  rounding — a clean vector chosen on purpose.

---

## protocol/protocol.c + protocol/protocol.h

**What they do.** The portable wire library compiled into *both* ends: a CRC,
a fixed 16-byte payload packer/unpacker, an HDLC-style framer, and a streaming
decoder. This is the contract the firmware and the (future) gateway share.

**Key decisions.**
- *CRC-16/CCITT-FALSE, bitwise (no table).* Poly `0x1021`, init `0xFFFF`, no
  reflection, xorout 0. The canonical check `"123456789" → 0x29B1` is a test
  vector. Bitwise (vs a 512-byte table) keeps the firmware small; speed is a
  non-issue at 100 Hz.
- *Explicit little-endian via shift/OR (G-3).* Pack/unpack build multi-byte
  fields with shifts and masks, never by casting a struct pointer at the
  buffer. That sidesteps both endianness assumptions and alignment UB, so the
  same bytes mean the same thing on the ARM node and an x86 gateway.
- *HDLC framing with byte stuffing.* `FLAG = 0x7E` marks frame boundaries;
  any `0x7E`/`0x7D` in the body is escaped as `0x7D` followed by `byte ^ 0x20`.
  The 2-byte CRC is appended **big-endian** before stuffing. Worst case (every
  byte needs escaping) is 38 bytes — the `SK_FRAME_MAX` the static buffers in
  `main.c` are sized for.
- *Streaming decoder is a state machine.* `sk_decoder_feed` consumes one byte
  at a time, tracking `in_frame`/`esc`/`overflow`, so a gateway reading a
  partial serial chunk can resync after garbage and reject bad-length / bad-CRC
  frames without ever indexing past its buffer.

**Study notes.**
- The *residue* property is a neat self-check: `CRC(payload ‖ crc_be) == 0`,
  tested directly in `test_crc16.c`.
- `test_framing.c` re-implements an independent reference stuffer and
  cross-checks it against the library — two implementations agreeing is much
  stronger evidence than one implementation agreeing with itself. It also
  exercises the nasty cases on purpose: a payload full of `0x7E/0x7D`, a frame
  split across two `feed` calls, and resync after junk.

---

## firmware/main.c + scripts/run_qemu.sh

**What they do.** `main.c` is the super-loop that wires everything together;
`run_qemu.sh` boots the resulting image and exposes its UART as a TCP socket.

**Key decisions (main.c).**
- *Static buffers only, no heap (REQ-FW-012).* `sim`, `bus`, `sample`,
  `payload[16]`, `frame[38]` are all `static` — the image links `-nostdlib`
  with no allocator, and static sizing makes worst-case memory obvious.
- *Fail loud, fail once.* If `adxl345_init` or `adxl345_read` fails, the node
  prints one ASCII error and halts in an infinite loop rather than spewing
  garbage frames. Under emulation a halt is visible; a flood of bad frames
  would hide the fault.
- *Boot banner is the one ASCII line.* `"Sharak node online\r\n"` is plain
  text on purpose — instant proof the image booted before the binary stream
  starts. Everything after it is framed telemetry.
- *Pacing is honest about being fake.* `delay_pace()` is a `volatile` busy-loop,
  explicitly NOT calibrated timing. QEMU has no real clock; a product would
  pace from a SysTick interrupt at exactly 100 Hz (a named later step). The
  `volatile` counter stops the optimizer deleting the empty loop.
- *`seq` is `uint16_t` and wraps* at `0xFFFF → 0x0000` by design; the protocol
  treats it as a rolling counter.

**Key decisions (run_qemu.sh).**
- `-M lm3s6965evb -nographic -kernel build/sharak_node.bin` boots the raw
  binary; `-serial tcp:127.0.0.1:5555,server,nowait` publishes UART0 as a TCP
  server that does **not** block waiting for a client, so the firmware runs
  whether or not you've connected. Read it with `nc 127.0.0.1 5555 | xxd`.

**Study notes.**
- The output is **binary**, not a log line. In the hexdump you can pick out the
  `7E` flags, the `01 01` version/type header, the little-endian `seq`, and
  `E8 03 00 00` (= 1000 mg) on Z. That tangible mapping from struct → bytes →
  hexdump is the payoff of the shift/OR packing.

---

## .github/workflows/ci.yml

**What it does.** Runs on every push to `main` and on PRs. Two jobs mirror the
two compilers: `host-tests` (`make test`, plain gcc, fast, no toolchain) and
`firmware-build` (`make firmware`, installs `gcc-arm-none-eabi`, proves the
image still cross-links).

**Key decisions.**
- *Split jobs for sharp signal.* A red `host-tests` is a logic bug; a red
  `firmware-build` is a portability/linker regression. One combined `make all`
  would blur the two.
- *No gateway/driver jobs yet.* Those milestones add their own jobs when they
  exist. CI describing things that aren't built would be noise.

---

## Environment limitations — what was and wasn't verified here

Honesty about the toolbox the code was authored in (G-9 is about trusting
primary sources, not the local machine):

- **Host tests: fully run and green.** `make test` executes all
  `tests/test_*.c` locally — 121 checks, 0 failed, 0 warnings (decode 11,
  driver 18, crc16 5, framing 61, i2c_sim 26). The pure logic (protocol, CRC,
  framing, decode, the sim) is therefore verified on real silicon-independent
  gcc.
- **Firmware cross-build / QEMU run: NOT executed here.** This machine has no
  `arm-none-eabi-gcc` and no `qemu-system-arm`, so `make firmware` and
  `scripts/run_qemu.sh` were authored to be cross-clean and documented, but
  **not** built or booted locally. CI (`firmware-build`) is the first place the
  cross-link actually runs. If it ever fails, the usual suspect is a
  `-nostdlib` libcall — see the `-fno-tree-loop-distribute-patterns` note.
- **Register values are from primary sources, not probed.** The UART, GPIO, and
  I2C register offsets/bits and the ADXL345 config come from the datasheets/
  spec (including the `I2CMCR` MFE-vs-SFE erratum), so the missing PDF tools on
  this box don't undermine them.
- **`handoff/` is intentionally left in place.** It holds the original prompt
  and the preserved pre-fix `adxl345.*`. The plan calls for deleting it in the
  final commit; since this work is staged for review rather than committed, the
  directory is kept so the reviewer can diff against the preserved originals.
  Delete `handoff/` when committing the final milestone.
