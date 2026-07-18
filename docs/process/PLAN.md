# Sharak — Capability Roadmap

**One system, one data path, many layers.** Sharak is a vertical slice of an
industrial telemetry platform for **factory-robot condition monitoring**: a fleet
of vibration sensors → bare-metal node firmware (C) → frozen wire protocol →
Linux gateway (C++17) → storage/exposure → (later) kernel driver, MQTT, analytics.

The spine is the data path:

> sensors → node firmware (C) → wire protocol → serial/TCP link → gateway (C++) → storage → query/exposure → fleet health

Each layer sits behind a stable contract so layers can evolve independently. The
frozen wire protocol is the firewall between node and gateway; the gateway's
stages are likewise decoupled so any one can be swapped without touching the others.

**Focus order:** **C++ → Linux.** Embedded extensions (FreeRTOS node variant,
real STM32) and IoT (MQTT, live view, fleet analytics, anomaly detection, Yocto/
OTA) are explicitly **parked** until the C++ gateway and the Linux layers land.

**Design backbone — ASPICE.** SWE.1 requirements: `../01_requirements_SWE1/`
(see `../01_requirements_SWE1/index.md`). SWE.2 architecture: `../02_architecture_SWE2/architecture.md`.
SWE.3 detailed design: added per stage as it is built. SWE.4–6: host tests,
integration, qualification, gated in CI.

**Execution — layer by layer.** Each stage is one vertical
slice: failing test → implementation → review → a single conventional commit.

---

## Delivered

A running sensor node, a rock-solid shared protocol, and a hardware-free fleet
load generator — all validated in CI.

- Repository scaffold, build system, CI, license.
- **Protocol library** in portable C: telemetry payload, CRC-16/CCITT-FALSE,
  HDLC byte-stuffed framing, streaming decoder, dependency-free unit tests.
- **Bare-metal firmware** for the QEMU `lm3s6965evb` (Cortex-M3): vector table,
  startup, linker script, UART0 driver, telemetry loop.
- **I2C dependency-injection seam** so the ADXL345 driver runs against a
  simulator on the host and the real peripheral on target, unchanged.
- **Fleet node simulator** (`sim/`): emits byte-exact frames via the same
  `protocol.c`, serves them over TCP, multi-instance for a multi-node load —
  so the gateway can be developed and tested with **no hardware**.
- CI: host tests (incl. the simulator's byte-exactness) + firmware cross-build.

## In development — the C++ gateway (M3)

A Linux daemon that ingests, validates, aggregates, and stores telemetry from a
fleet of nodes. Decoupled stages, built in this commit order:

1. **Decode + verify** — reuse `protocol.c` via the `extern "C"` seam.
2. **Error model + telemetry types** — `enum class`, `std::optional`, RAII value type.
3. **FrameParser** — streaming HDLC reassembly (`std::byte`/`std::span`, TDD).
4. **Store interface + in-memory backend** — dependency inversion.
5. **TCP transport** — fds/termios, RAII fd wrapper; live ingest from the QEMU node / simulators. *(Linux begins here.)*
6. **SQLite store backend** — persistence behind the Store interface.
7. **Exposure** — query CLI (last-N, per-node stats); MQTT publisher stubbed.
8. **Integrate pipeline + multi-node** — DI wiring. **M3 milestone.**

Requirements: `../01_requirements_SWE1/gateway.md`. Each milestone gate = rebuild the
load-bearing pieces solo + defend cold.

## Planned

- **Linux character-device driver (M4)**: `/dev/sharak0` exposing
  open/read/write/release, built against kernel headers (`../01_requirements_SWE1/driver.md`).
- **Embedded extensions** *(parked)*: FreeRTOS node variant; port to real STM32 hardware.
- **IoT layer** *(parked)*: MQTT publish to a broker; terminal/web live view;
  per-node fleet health analytics; on-device anomaly detection (TinyML); Yocto/OTA.

---

## Design principles

- **Layered, contract-first.** Each layer talks to its neighbours through a
  stable, documented contract. The wire protocol is frozen; gateway stages
  depend on interfaces, not concrete implementations.
- **Sensor-agnostic core.** Transport and FrameParser never open the payload, so
  new sensor types (`type` field) add no code there — only Decode/Store/Exposure
  learn a new type (`../01_requirements_SWE1/gateway.md` §0).
- **Test what matters.** Every pure component carries unit tests; CI fails on any
  failing test.
- **Build to grow.** The roadmap reads as a product that is actively expanding,
  each layer independently swappable.
