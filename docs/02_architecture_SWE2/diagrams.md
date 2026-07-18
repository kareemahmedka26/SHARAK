# Sharak — System Diagrams

These diagrams render automatically on GitHub. For a polished one-page visual,
open [`diagram.html`](diagram.html) in a browser.

> The orange/“shared protocol” box is the single `protocol.c` compiled into
> **both** the node firmware and the Linux gateway — so the two ends can never
> disagree on the wire format.

---

## 1. System architecture

```mermaid
flowchart LR
    subgraph Node["Sensor Node — bare-metal C, ARM Cortex-M3 (QEMU)"]
        direction TB
        S["Sensor (simulated)<br/>vibration · 3-axis accel (ADXL345)"]
        B["Telemetry builder"]
        E["Protocol: encode<br/>CRC-16 + framing"]
        U["UART TX"]
        S --> B --> E --> U
    end

    subgraph GW["Linux Gateway — modern C++17"]
        direction TB
        R["Serial reader"]
        P["FrameParser: decode + CRC verify"]
        ST[("SQLite store")]
        A["Anomaly check"]
        R --> P --> ST --> A
    end

    subgraph Serve["Serve & Integrate"]
        direction TB
        CLI["Query CLI"]
        MQ["MQTT publish"]
        DASH["Live view"]
        DRV["Linux driver /dev/sharak0"]
    end

    U -- "framed bytes 0x7E..0x7E" --> R
    A --> CLI
    A --> MQ
    A --> DASH
    E -. "same protocol.c" .- P
```

---

## 2. How one reading travels (sequence)

```mermaid
sequenceDiagram
    participant Sensor
    participant FW as Node firmware
    participant Link as Serial link
    participant GW as Gateway
    participant DB as SQLite

    Sensor->>FW: raw reading
    FW->>FW: build telemetry + status flags
    FW->>FW: encode (CRC-16 + byte-stuffing)
    FW->>Link: framed bytes (0x7E .. 0x7E)
    Link->>GW: byte stream (may be split / noisy)
    GW->>GW: FrameParser reassembles a full frame
    GW->>GW: verify CRC + decode
    GW->>DB: store reading
    GW-->>GW: raise alert if over threshold
```

---

## 3. Platform layering — one core, many applications

```mermaid
flowchart TB
    subgraph Profiles["Application profiles (config only)"]
        IND["Industrial ★<br/>predictive maintenance"]
        AG["Agriculture<br/>soil & climate"]
        HC["Healthcare<br/>patient vitals"]
    end

    subgraph Core["Sharak core platform (reusable)"]
        FW["Node firmware"]
        PR["Shared protocol"]
        GWC["Gateway: ingest · store · serve"]
    end

    IND --> Core
    AG --> Core
    HC --> Core
    FW --- PR --- GWC
```

The applications differ only in *what the channels mean* and *what thresholds
apply* — the firmware, protocol, and gateway code stay the same.
