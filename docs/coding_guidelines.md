# Sharak — Coding Guidelines

These rules are binding for all code in this repository. Every rule exists
because it prevents a class of real embedded bugs; the rationale is recorded
next to each rule so the rule can be questioned intelligently later.

- **G-1** Multiply before divide in integer scaling.
- **G-2** No floats in firmware.
- **G-3** Explicit shift/OR byte assembly, never type-punning for wire data.
- **G-4** Public APIs in fixed-point engineering units (milli-g).
- **G-5** Representation resolution ≤ sensor resolution.
- **G-6** Signed narrowing casts: explicit only, implementation-defined
  (C17 §6.3.1.3p3), documented GCC wrap, never called portable.
- **G-7** Sign reconstruction via `(int32_t)(int16_t)u16`.
- **G-8** Every pure function has a host unit test with nonzero-exit failure.
- **G-9** Test vectors derived from primary sources.
- **G-10** Unit tests in the layer's language (C); Python only for
  orchestration (not in this task).

---

## Rationale, rule by rule

**G-1 — Multiply before divide.** Integer division truncates. `raw * 125 / 32`
keeps the full numerator before the single truncating division;
`raw / 32 * 125` would throw away up to 31/32 of an LSB *before* scaling and
produce results that are wrong by up to ~121 mg. The price is that the
intermediate must not overflow — which is why the multiply is done in
`int32_t` (max |raw| is 4095 in FULL_RES ±16 g, and 4095 × 125 fits easily).

**G-2 — No floats in firmware.** The LM3S6965 has no FPU; every `float` op
becomes a hidden call into a software FP library (code size, nondeterministic
latency, and a libgcc dependency we deliberately don't link with `-nostdlib`).
Fixed-point integer math is exact, fast, and auditable.

**G-3 — Shift/OR, never type-punning.** `(uint16_t)lo | ((uint16_t)hi << 8)`
is defined behavior on every platform and makes endianness *visible in the
code*. Casting a byte buffer to `uint16_t*` is undefined behavior (strict
aliasing, alignment) and silently assumes the CPU's endianness equals the
wire's.

**G-4 — Engineering units at API boundaries.** Public functions speak
milli-g (`int32_t x_mg`), not "raw LSBs", so callers never need to know the
sensor's scale factor. One conversion, in one place, tested once.

**G-5 — Representation resolution ≤ sensor resolution.** The ADXL345 in
FULL_RES resolves 3.90625 mg/LSB; we represent in 1 mg steps. Representing
*coarser* than the sensor would destroy information; 1 mg ≤ 3.90625 mg, so
nothing is lost.

**G-6 — Signed narrowing casts.** Converting a value that doesn't fit into a
signed type is *implementation-defined* in C17 (§6.3.1.3p3). GCC documents
modulo-2^N wrapping, which we rely on — but each such cast must be explicit,
commented at the call site, and never described as "portable C".

**G-7 — Sign reconstruction.** `(int32_t)(int16_t)u16` is the canonical,
two-step way to reinterpret a 16-bit two's-complement wire value: first the
implementation-defined narrowing (G-6) reconstructs the sign, then the
widening to `int32_t` is fully defined (sign-extension). Skipping the inner
cast is *the* classic bug: `0xFFFF` becomes +65535 instead of −1.
(This exact bug existed in this repo once — see `git log firmware/adxl345.c`.)

**G-8 — Host tests for pure functions.** Anything with no hardware dependency
must be testable with plain `gcc` on the developer PC, and the test must exit
nonzero on failure so `make test` and CI can gate on it.

**G-9 — Test vectors from primary sources.** Expected values in tests are
derived from the datasheet/spec math (shown in comments), never by running
the implementation and pasting its own output back in — that would only prove
the code agrees with itself.

**G-10 — Tests in the layer's language.** The C modules are tested in C, with
the same integer semantics as production. Where Python appears, it orchestrates;
it never re-implements protocol logic for testing.
