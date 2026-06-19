# Sharak — Requirements (C++17 Linux gateway + fleet simulator)

Style: ASPICE SWE.1-flavored numbered requirements. Each is testable; the
traceability table at the bottom maps every requirement to the artifact that
implements it and the test (or inspection) that verifies it. Stages not yet
built are marked `(pending — <stage> commit)`.

Scope: the Linux gateway (`REQ-GW`) that ingests, validates, stores, and exposes
telemetry from a fleet of nodes, and the development fleet simulator (`REQ-SIM`)
that feeds it byte-exact frames without hardware. The node firmware, protocol,
and build/CI are specified in `requirements.md`; the kernel driver in
`requirements_driver.md`.

The gateway consumes exactly one thing from the node: the **frozen wire format**
(`requirements.md` §2 / `architecture.md` §4). That contract is the firewall;
the gateway reuses the same `protocol.c` so the two ends can never disagree.

---

## 0. Telemetry model & extensibility (REQ-GW-MODEL)

- **MODEL-001** Every reading SHALL be represented as a common **envelope**
  shared by all sensor types — `node_id`, `recv_time` (gateway receive
  timestamp), `seq`, `ver`, `type` — plus a **type-specific payload**.
- **MODEL-002** `node_id` SHALL be assigned by the gateway from the transport
  endpoint (REQ-GW-TR-002); it is NOT carried on the wire.
- **MODEL-003** `recv_time` SHALL be stamped by the gateway on arrival, because
  the wire payload carries no timestamp; it is required for time-ordered trends.
- **MODEL-004** The payload SHALL be selected by `type`. A **sensor-type
  registry** SHALL be maintained (see table below); adding a sensor type SHALL
  NOT change the envelope nor the existing types' contracts.
- **MODEL-005** Only stages that interpret payload meaning (Decode, Store,
  Exposure) may depend on `type`; Transport and FrameParser SHALL remain
  payload-agnostic and therefore sensor-agnostic.

### Sensor-type registry

| `type` | Sensor | Payload | Status |
|---|---|---|---|
| `0x01` | Accelerometer (vibration) | `x_mg, y_mg, z_mg` (int32 LE) | Active (frozen, `requirements.md` §2) |
| `0x02` | Temperature | `temp_mC` | Reserved (future) |
| `0x03` | Current / load | `current_mA` | Reserved (future) |

---

## 1. General (REQ-GW-GEN)

- **GEN-001** The gateway SHALL be C++17, built with CMake on Linux, compiled
  `-Wall -Wextra -Werror`.
- **GEN-002** The gateway SHALL reuse `protocol/src/protocol.c` for ALL wire
  decode/verify via an `extern "C"` linkage seam, and SHALL NOT re-implement the
  wire format. (`protocol.h` already provides the `__cplusplus` guard.)
- **GEN-003** The gateway SHALL NOT use exceptions for control flow; errors SHALL
  be reported via `enum class` status and `std::optional`-style returns.
- **GEN-004** Each pipeline stage SHALL depend on an abstract interface, not a
  concrete type (dependency inversion), to permit test fakes and backend swaps.
- **GEN-005** Resource ownership SHALL be RAII; owning raw pointers are
  prohibited; rule-of-zero types preferred.

## 2. Transport (REQ-GW-TR)

- **TR-001** Transport SHALL read a stream of framed bytes from each node over a
  TCP connection, structured so a serial/file backend can replace it behind the
  same interface.
- **TR-002** The gateway SHALL support N concurrent node connections; each node's
  identity SHALL be derived from its transport endpoint, NOT from payload bytes.
- **TR-003** Transport SHALL own OS file descriptors via an RAII wrapper that
  closes them on destruction, with no fd leak on any error path.
- **TR-004** Transport SHALL deliver bytes as received (arbitrary chunk
  boundaries) and SHALL NOT assume frame alignment.

## 3. FrameParser (REQ-GW-FP)

- **FP-001** FrameParser SHALL reassemble HDLC frames from an incrementally-fed
  byte stream (flag/escape/overflow state), byte-identical to `protocol.c`.
- **FP-002** It SHALL resynchronize on the next `0x7E` after garbage, a bad
  escape, an over-long frame, or a CRC failure, reporting each distinctly.
- **FP-003** It SHALL emit only CRC-validated 16-byte payloads downstream;
  malformed frames SHALL never propagate.
- **FP-004** It SHALL operate over `std::span`/`std::byte` on bounded buffers
  (`SK_FRAME_MAX`); no per-byte heap allocation.

## 4. Decode + verify (REQ-GW-DV)

- **DV-001** Decode SHALL call `protocol.c` (via the `extern "C"` seam) to verify
  the CRC and unpack a validated payload into a typed telemetry value.
- **DV-002** It SHALL verify CRC **before** unpacking, and SHALL reject unknown
  `ver`/`type` with a distinct error.
- **DV-003** On failure it SHALL return a status and SHALL NOT write its output
  parameter; the output is valid only on `Ok`.
- **DV-004** The decoded telemetry SHALL be a value type carrying the node
  identity supplied by the transport (the envelope, REQ-GW-MODEL-001).

## 5. Store (REQ-GW-ST)

- **ST-001** Store SHALL be an interface exposing at least `append(reading)`,
  `last_n(node, n)`, `stats(node)`.
- **ST-002** An in-memory backend SHALL implement Store for tests/dev; a SQLite
  backend SHALL implement it for persistence — selectable by DI without touching
  ingest.
- **ST-003** Readings SHALL be keyed by `(node_id, type, seq)` and held in
  time order; per-node statistics (count, per-axis min/max/mean) SHALL be
  computable.

## 6. Exposure (REQ-GW-EX)

- **EX-001** A query CLI SHALL expose last-N readings and per-node statistics
  from the Store.
- **EX-002** A cloud/MQTT publisher SHALL exist as an interface with a stub
  implementation (no broker dependency yet).

## 7. Build & CI (REQ-GW-BU)

- **BU-001** The top-level `Makefile` SHALL build the fleet simulator (host
  `gcc`, `-Werror`); gateway CMake targets are added per stage as implemented.
- **BU-002** CI SHALL build the simulator and run its byte-exactness test on
  every push (the `tests/test_*.c` wildcard auto-includes it).

## 8. Fleet simulator (REQ-SIM) — development scaffolding

- **SIM-001** The simulator SHALL emit telemetry frames BYTE-EXACT with real
  firmware by encoding through the same `protocol.c` (no bespoke byte generator).
- **SIM-002** Each instance SHALL expose its byte stream over TCP as a server
  (mirroring `scripts/run_qemu.sh`'s `-serial tcp:HOST:PORT,server,nowait`), with
  configurable port and rate.
- **SIM-003** Multiple instances SHALL be launchable to present a multi-node
  load; distinct ports represent distinct node identities.
- **SIM-004** The simulator SHALL be pure C linking `protocol.c` with C linkage,
  leaving the gateway's `extern "C"` decode seam as the gateway's responsibility.
- **SIM-005** A host unit test SHALL assert the simulator's output is correct
  independently of itself: emitted frames round-trip through `protocol.c`'s
  decoder to the same `(seq,x,y,z)`, AND are byte-identical to
  `sk_payload_pack` + `sk_frame_encode`.

---

## Traceability

| Requirement | Implemented in | Verified by |
|---|---|---|
| REQ-GW-MODEL-001..005 | (pending — Decode/Store commits) | (pending) |
| REQ-GW-GEN-001..005 | (pending — gateway stage commits) | (pending) |
| REQ-GW-TR-001..004 | (pending — transport commit) | (pending) |
| REQ-GW-FP-001..004 | (pending — frameparser commit) | (pending) |
| REQ-GW-DV-001..004 | (pending — decode commit) | (pending) |
| REQ-GW-ST-001..003 | (pending — store commits) | (pending) |
| REQ-GW-EX-001..002 | (pending — exposure commit) | (pending) |
| REQ-GW-BU-001 | `Makefile` (`sim` target) | CI `make sim` / local build |
| REQ-GW-BU-002 | `.github/workflows/ci.yml`, `tests/test_fleet_node.c` | CI `make test` |
| REQ-SIM-001..004 | `sim/fleet_node.c`, `sim/fleet_main.c` | `tests/test_fleet_node.c`; manual `nc 127.0.0.1 PORT \| xxd` |
| REQ-SIM-005 | `tests/test_fleet_node.c` | `make test` (12 checks) |
