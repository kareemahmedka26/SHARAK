# Sharak — Requirements Index (ASPICE SWE chain)

Sharak's requirements are split by stage, all in the same ASPICE SWE.1 style
(numbered, testable `SHALL` requirements + a traceability table). Together they
specify the whole platform.

| Doc | Scope | Status |
|---|---|---|
| [`requirements.md`](requirements.md) | Node firmware (`REQ-FW`), portable protocol (`REQ-PR`), build & CI (`REQ-BU`), verification (`REQ-TS`) | Delivered |
| [`requirements_gateway.md`](requirements_gateway.md) | C++17 Linux gateway (`REQ-GW`) + fleet simulator (`REQ-SIM`) | Gateway in development; sim delivered |
| [`requirements_driver.md`](requirements_driver.md) | Linux kernel char driver (`REQ-DRV`) | Planned |

**ASPICE chain across the repo:**

- **SWE.1 Software Requirements** — the three docs above.
- **SWE.2 Software Architecture** — [`architecture.md`](architecture.md) (layering, contracts, decisions).
- **SWE.3 Software Detailed Design** — added per stage as it is built (interfaces, types, error model, ownership).
- **SWE.4 Unit Verification** — `tests/` (host unit tests; CI gates on them).
- **SWE.5 Integration / SWE.6 Qualification** — multi-node end-to-end against `REQ-GW`, gated in CI.
