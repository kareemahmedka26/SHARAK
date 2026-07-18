# Sharak

**An industrial IIoT condition-monitoring system — edge to cloud.**
A fleet of bare-metal sensor nodes (C) → a frozen binary wire protocol → a layered Linux gateway (C++17) → a planned cloud dashboard and on-edge failure detection. Built for predictive maintenance of factory machines, as three decoupled tiers behind a frozen wire contract.

![CI](https://img.shields.io/badge/CI-pending-lightgrey)
![Language](https://img.shields.io/badge/C-C%2B%2B-blue)
![Platform](https://img.shields.io/badge/ARM%20Cortex--M%20%7C%20Linux-informational)
![License](https://img.shields.io/badge/license-MIT-green)

> Sharak models a fleet of industrial sensor nodes that stream vibration
> telemetry to a Linux gateway, which validates, aggregates, stores, and serves
> that data — the foundation for condition monitoring and predictive
> maintenance. The whole vertical slice — firmware, transport, OS, tooling — is
> one cohesive system.

No physical hardware required: the firmware runs on a real ARM Cortex-M3 core
**emulated in QEMU**, so the entire system builds and runs on a normal PC.

---

## Architecture

```mermaid
flowchart LR
    subgraph Node["Sensor Node (bare-metal C, QEMU Cortex-M3)"]
        S[Synthetic sensors] --> FW[Firmware]
        FW --> ENC[protocol: encode + CRC16 + framing]
    end
    ENC -- "framed bytes over UART/serial" --> GW
    subgraph Gateway["Linux Gateway (C++17)"]
        GW[Serial reader] --> DEC[protocol: decode + verify]
        DEC --> DB[(SQLite store)]
        DB --> API[Query CLI / MQTT]
    end
    DEC -. shares the exact same .c .-> ENC
```

The **`protocol/`** library is the heart of the system: a single portable C
file (`protocol.c`) compiled into *both* the firmware and the gateway, so the
two ends can never disagree about the wire format.

📐 **More diagrams:** see [`docs/02_architecture_SWE2/diagrams.md`](docs/02_architecture_SWE2/diagrams.md) (architecture,
sequence, and platform-layering views that render on GitHub), or open
[`docs/02_architecture_SWE2/diagram.html`](docs/02_architecture_SWE2/diagram.html) for a polished one-page overview.

---

## Repository layout

| Path         | What it is                                                            | Status        |
|--------------|-----------------------------------------------------------------------|---------------|
| `protocol/`  | Portable C library: telemetry message, CRC-16, HDLC-style framing + tests | ✅ Working    |
| `firmware/`  | Bare-metal Cortex-M3 node firmware (startup, linker, UART) for QEMU    | ✅ Builds/runs |
| `gateway/`   | Linux gateway daemon in C++17 (serial → parse → store → serve)         | 🚧 In development |
| `driver/`    | Linux character-device driver exposing a virtual sensor                | 📋 Planned    |
| `.github/`   | CI: run host unit tests + cross-build the node image (`make all`)     | ✅ Configured |
| `docs/`      | Plan and architecture notes                                           | ✅            |

---

## Quick start

Everything is driven from the **top-level `Makefile`**. The two halves build
with two different compilers on purpose (see the header comment in the
`Makefile`): the portable subset is compiled by host `gcc` for the tests and by
`arm-none-eabi-gcc` for the image.

### 1. Host unit tests (no special tools needed)

```bash
make test
```

Builds and runs every `tests/test_*.c` (protocol CRC + framing, the ADXL345
decode/driver against the I2C simulator). You should finish on:

```
ALL HOST TESTS PASSED
```

### 2. Cross-compile the node image (needs the ARM toolchain)

```bash
sudo apt install gcc-arm-none-eabi    # one-time setup
make firmware                         # -> build/sharak_node.elf + .bin
```

### 3. Run the node in QEMU (needs qemu-system-arm)

```bash
sudo apt install qemu-system-arm      # one-time setup
scripts/run_qemu.sh                   # UART0 -> tcp://127.0.0.1:5555
```

Then, from a second terminal, read the serial stream:

```bash
nc 127.0.0.1 5555 | xxd | head
```

The node prints one ASCII banner line on boot, then emits **binary** telemetry
frames (it is a wire protocol, not a console log). Each frame is HDLC-framed —
a `0x7E` flag, the byte-stuffed 16-byte payload + 2-byte CRC, then a closing
`0x7E` — so the hexdump looks like:

```
Sharak node online
00000000: 7e01 0100 0000 0000 0000 1000 0000 e803  ~...............
00000010: 0000 <crc> 7e7e 01 01 ...                ......~~....
          ^ver ^type ^seq  ^x_mg(LE)  ^y_mg ^z_mg  ^crc ^flag
```

(`z_mg = 0x000003E8 = 1000` — the simulator's steady +1 g on Z; X/Y carry the
small synthetic wobble.)

---

## Roadmap

Sharak is a **three-tier system** built as a vertical slice that grows outward.

### Tier 1 · Sensor node — bare-metal C · ✅ built
- [x] **Protocol core** — message format, CRC-16/CCITT, byte-stuffed framing, unit tests
- [x] **Node firmware** — bare-metal Cortex-M3, custom startup + linker script, UART driver
- [x] **CI** — protocol tests + firmware/gateway builds on every push
- [ ] **Second sensor type** (temperature) — exercises the multi-type wire contract

### Tier 2 · Gateway — C++17 on Linux · 🚧 in progress
- [ ] **Multi-node TCP ingest** + streaming frame parser (shared `protocol.c` via an `extern "C"` seam)
- [ ] **Store interface + SQLite** backend, with an in-memory fake for tests
- [ ] **Exposure query CLI**; concurrent fleet handling (node identity from the transport)

### Tier 3 · Cloud & Edge AI · 📋 planned
- [ ] **Cloud dashboard** — gateway publishes over MQTT → time-series store → Grafana
- [ ] **Edge AI** — on-gateway inference for millisecond failure detection; latency-critical decisions stay at the edge instead of round-tripping to the cloud

**Further out:** FreeRTOS node, real STM32 hardware, on-node TinyML, Yocto / OTA, a Linux character-device driver.

See [`docs/process/PLAN.md`](docs/process/PLAN.md) for the capability roadmap and
[`docs/02_architecture_SWE2/architecture.md`](docs/02_architecture_SWE2/architecture.md) for design details.

---

## License

MIT — see [LICENSE](LICENSE).
