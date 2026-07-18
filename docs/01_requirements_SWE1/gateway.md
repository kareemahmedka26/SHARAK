# Sharak — Requirements (C++17 Linux gateway + fleet simulator)

Style: ASPICE SWE.1-flavored numbered requirements. Each is testable; the
traceability table at the bottom maps every requirement to the artifact that
implements it and the test (or inspection) that verifies it. Stages not yet
built are marked `(pending)`.

Scope: the Linux gateway (`REQ-GW`) that ingests, validates, stores, and exposes
telemetry from a fleet of nodes, and the development fleet simulator (`REQ-SIM`)
that feeds it byte-exact frames without hardware. The node firmware, protocol,
and build/CI are specified in `node.md`; the kernel driver in
`driver.md`.

Out of scope for this release: the cloud dashboard and the on-gateway edge-AI
stage. Both are roadmap (`../02_architecture_SWE2/architecture.md` §10); the cloud seam exists as an
interface with a stub implementation, the implementation does not.

The gateway consumes exactly one thing from the node: the **frozen wire format**
(`node.md` §2 / `../02_architecture_SWE2/architecture.md` §4). That contract is the firewall;
the gateway reuses the same `protocol.c` so the two ends can never disagree.

> **TBD** marks a value or decision that is still provisional — a working
> assumption, to be confirmed by measurement or design review before it is
> treated as binding.

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
- **MODEL-006** The payload SHALL be **variable length**, bounded by
  `SK_PAYLOAD_MIN`..`SK_PAYLOAD_MAX`. Length is derived from the frame, not from
  a length field on the wire.

### Sensor-type registry

| `type` | Sensor | Payload body | Total | Status |
|---|---|---|---|---|
| `0x01` | Accelerometer (vibration) | `x_mg, y_mg, z_mg` (int32 LE) | 16 B | **Active** |
| `0x02` | Temperature | `temp_centi_c` (int16 LE, centi-°C) | 6 B | **Active** |
| `0x03` | Current / load | `current_ma` (int32 LE, milli-amps) | 8 B | **Active** |

---

## 1. General (REQ-GW-GEN)

- **GEN-001** The gateway SHALL be C++17, built with CMake on Linux, compiled
  `-Wall -Wextra -Werror`.
- **GEN-002** The gateway SHALL reuse `protocol/src/protocol.c` for ALL wire
  decode/verify via an `extern "C"` linkage seam, and SHALL NOT re-implement the
  wire format.
- **GEN-003** The gateway SHALL NOT use exceptions for control flow; errors SHALL
  be reported via `enum class` status and `std::optional`-style returns.
- **GEN-004** Each pipeline stage SHALL depend on an abstract interface, not a
  concrete type (dependency inversion), to permit test fakes and backend swaps.
- **GEN-005** Resource ownership SHALL be RAII; owning raw pointers are
  prohibited; rule-of-zero types preferred.

## 2. Non-functional (REQ-GW-NFR)  TBD

Proposed budgets. They exist so "finished" is objective and so the transport and
storage stages have targets to design against.

- **NFR-001** The gateway SHALL sustain **16 concurrent nodes**.
  *(Rationale: one factory cell; comfortably exercised by the fleet simulator.)*
- **NFR-002** It SHALL sustain **100 frames/second per node**
  (≈1,600 frames/s aggregate) without frame loss.
  *(Rationale: matches the node's 100 Hz sample rate.)*
- **NFR-003** End-to-end latency from last byte received to reading
  durably stored SHALL be **< 50 ms (p99)**.
- **NFR-004** Steady-state memory SHALL be **< 32 MB RSS** at
  NFR-001 load, with **no growth over a 24-hour run** (leak-free).
- **NFR-005** Per-node buffered state SHALL be **bounded and
  statically sized**; no unbounded per-connection allocation.
- **NFR-006** Startup to accepting connections SHALL be **< 1 s**.
- **NFR-007** The gateway SHALL run **24 hours** under simulated load without
  restart, crash, or file-descriptor growth.

## 3. Error model (REQ-GW-ERR)  TBD

The governing principle: **a bad frame is an expected event on a real bus, not
an exceptional one.** Corruption must never cost a connection or a process.

- **ERR-001** A frame failing CRC SHALL be **dropped**; the connection SHALL
  remain open and the parser SHALL resynchronize on the next flag.
- **ERR-002** A payload with an unknown `type` SHALL be **dropped and counted**,
  not treated as fatal — a newer node must not be able to kill the gateway.
- **ERR-003** A payload with an unrecognized `ver` SHALL be dropped and counted.
- **ERR-004** A frame exceeding the maximum inner length SHALL be dropped, and
  the parser SHALL resynchronize; the buffer SHALL NOT overflow.
- **ERR-005** A node disconnecting SHALL close only that connection's resources;
  the gateway SHALL continue serving other nodes and SHALL accept the node's
  reconnection without restart.
- **ERR-006** A Store write failure SHALL be logged and counted; it SHALL NOT
  crash the process and SHALL NOT block ingest indefinitely.
- **ERR-007** Every drop category above SHALL be **counted per node** and
  reportable via Exposure — silent data loss is prohibited.

## 4. Concurrency model (REQ-GW-CON)  TBD

- **CON-001** Ingest SHALL use a **single-threaded event loop
  (`epoll`)** serving all node connections.
  *Rationale:* at 16 nodes there is no throughput need for threads, and a single
  thread removes shared mutable state entirely — no mutexes, no data races, and
  deterministic, reproducible tests. The classic Linux answer, and the honest
  senior one: the cheapest concurrency bug is the one that cannot exist.
  *Alternative rejected:* thread-per-node, which would require locking the Store
  and make failures timing-dependent.
- **CON-002** Per-node parser state SHALL be owned solely by the event loop; no
  parser state SHALL be shared across connections.
- **CON-003** If storage later proves too slow to run inline, the design SHALL
  evolve by adding a bounded queue and one writer thread — not by making the
  ingest path multi-threaded.

## 5. Transport (REQ-GW-TR)

- **TR-001** Transport SHALL read a stream of framed bytes from each node over a
  TCP connection, structured so a serial/file backend can replace it behind the
  same interface.
- **TR-002** The gateway SHALL support N concurrent node connections (N per
  NFR-001); each node's identity SHALL be derived from its transport endpoint,
  NOT from payload bytes.
- **TR-003** Transport SHALL own OS file descriptors via an RAII wrapper that
  closes them on destruction, with no fd leak on any error path.
- **TR-004** Transport SHALL deliver bytes as received (arbitrary chunk
  boundaries) and SHALL NOT assume frame alignment.

## 6. FrameParser (REQ-GW-FP)

- **FP-001** FrameParser SHALL reassemble HDLC frames from an incrementally-fed
  byte stream (flag/escape/overflow state), byte-identical to `protocol.c`.
- **FP-002** It SHALL resynchronize on the next `0x7E` after garbage, a bad
  escape, an over-long frame, or a CRC failure, reporting each distinctly.
- **FP-003** It SHALL emit only CRC-validated payloads downstream, of length
  `SK_PAYLOAD_MIN`..`SK_PAYLOAD_MAX`; malformed frames SHALL never propagate.
- **FP-004** It SHALL operate on bounded buffers (`SK_FRAME_MAX`); no per-byte
  heap allocation.

## 7. Decode (REQ-GW-DV)

- **DV-001** Decode SHALL turn a CRC-validated payload into a typed telemetry
  value, dispatching on `type` via the `extern "C"` seam to `protocol.c`.
- **DV-002** CRC verification SHALL happen at the frame layer (FP-003), not in
  Decode; Decode SHALL validate `ver`, `type`, and that the payload is long
  enough for its declared type.
- **DV-003** On failure it SHALL return a status and SHALL NOT write its output
  parameter; the output is valid only on `Ok`.
- **DV-004** The decoded telemetry SHALL be a value type carrying the node
  identity supplied by the transport (the envelope, MODEL-001).

## 8. Store (REQ-GW-ST)

- **ST-001** Store SHALL be an interface exposing at least `append(reading)`,
  `last_n(node, n)`, `stats(node)`.
- **ST-002** An in-memory backend SHALL implement Store for tests/dev; a SQLite
  backend SHALL implement it for persistence — selectable by DI without touching
  ingest.
- **ST-003** Readings SHALL be keyed by `(node_id, type, seq)` and held in
  time order; per-node statistics SHALL be computable per sensor type.

## 9. Exposure (REQ-GW-EX)

- **EX-001** A query CLI SHALL expose last-N readings and per-node statistics
  from the Store.
- **EX-002** A cloud/MQTT publisher SHALL exist as an interface with a stub
  implementation (no broker dependency).
- **EX-003** Per-node error counters (REQ-GW-ERR-007) SHALL be reportable.

## 10. Build & CI (REQ-GW-BU)

- **BU-001** The gateway SHALL build via CMake; the fleet simulator via the
  top-level `Makefile` (host `gcc`, `-Werror`).
- **BU-002** CI SHALL build and run all host tests on every push.

## 11. Fleet simulator (REQ-SIM) — development scaffolding

- **SIM-001** The simulator SHALL emit telemetry frames BYTE-EXACT with real
  firmware by encoding through the same `protocol.c`.
- **SIM-002** Each instance SHALL expose its byte stream over TCP as a server,
  with configurable port and rate.
- **SIM-003** Multiple instances SHALL be launchable to present a multi-node
  load; distinct ports represent distinct node identities.
- **SIM-004** The simulator SHALL be pure C linking `protocol.c` with C linkage.
- **SIM-005** A host unit test SHALL assert the simulator's output is correct
  independently of itself.
- **SIM-006** The simulator SHALL be able to emit all three active
  sensor types, so multi-type ingest can be exercised without hardware.

---

## Traceability

| Requirement | Implemented in | Verified by |
|---|---|---|
| REQ-GW-MODEL-001..006 | `protocol.c` envelope + type registry | `tests/test_multitype.c` (24 checks) |
| REQ-GW-GEN-001..005 | `gateway/CMakeLists.txt`, stage sources | `ctest` (build is `-Werror`) |
| REQ-GW-NFR-001..007 | (pending) | (pending — load test) |
| REQ-GW-ERR-001..004 | `protocol.c` decoder | `tests/test_framing.c` (61 checks) |
| REQ-GW-ERR-005..007 | (pending) | (pending) |
| REQ-GW-CON-001..003 | (pending) | (pending) |
| REQ-GW-TR-001..004 | (pending) | (pending) |
| REQ-GW-FP-001..004 | (pending) | (pending) |
| REQ-GW-DV-001..004 | `gateway/src/decode.cpp` | `gateway/tests/test_decode.cpp` |
| REQ-GW-ST-001..003 | (pending) | (pending) |
| REQ-GW-EX-001..003 | (pending) | (pending) |
| REQ-GW-BU-001..002 | `Makefile`, `gateway/CMakeLists.txt`, CI | CI |
| REQ-SIM-001..005 | `sim/fleet_node.c`, `sim/fleet_main.c` | `tests/test_fleet_node.c` |
| REQ-SIM-006 | (pending) | (pending) |
