# Gateway (C++17, Linux)

The Linux-side counterpart to the node: it ingests framed telemetry from a
**fleet** of nodes, validates it, stores it, and exposes it. It is built
**stage by stage**, each stage behind a stable interface so any one can be
swapped (test fakes, SQLite, MQTT) without touching its neighbours:

```
Transport → FrameParser → Decode+verify → Store → Exposure
```

- **Transport** — reads framed bytes from each node over TCP; node identity is
  derived from the connection, not the payload.
- **FrameParser** — streaming, byte-at-a-time HDLC reassembly into validated payloads.
- **Decode + verify** — reuses the exact `protocol.c` via an `extern "C"` seam, so
  the gateway and firmware can never disagree about the wire format.
- **Store** — persists readings behind a storage interface (in-memory, then SQLite).
- **Exposure** — query CLI for last-N readings and per-node statistics, with a
  cloud/MQTT publisher stubbed behind an interface.

Per-stage requirements: [`docs/requirements_gateway.md`](../docs/requirements_gateway.md).
Architecture: [`docs/architecture.md`](../docs/architecture.md) §9.

> No stage code yet — the stages are added one commit at a time. The fleet
> simulator in `sim/` (byte-exact via `protocol.c`) provides the multi-node load
> to develop and test the gateway against, with no hardware.
