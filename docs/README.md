# Sharak — documentation map

Organised by **ASPICE software engineering process**, with one file per
component. ASPICE names processes, not filenames — so `SWE.1` is the *activity*
(Software Requirements Analysis) and the file below is its work product.

| Folder | ASPICE | Contains |
|---|---|---|
| [`01_requirements_SWE1/`](01_requirements_SWE1/) | **SWE.1** Requirements analysis | What the system SHALL do, per component |
| [`02_architecture_SWE2/`](02_architecture_SWE2/) | **SWE.2** Architectural design | How it decomposes: components, interfaces, data flow, decisions |
| [`03_detailed_design_SWE3/`](03_detailed_design_SWE3/) | **SWE.3** Detailed design | How each unit works internally |
| `../tests/`, `../gateway/tests/` | **SWE.4** Unit verification | The tests themselves |
| [`reference/`](reference/) | — | Datasheets and schematics |
| [`process/`](process/) | — | Roadmap, coding guidelines, working notes |

## Requirements (SWE.1)

| File | Component |
|---|---|
| [`index.md`](01_requirements_SWE1/index.md) | Index of all requirement sets |
| [`node.md`](01_requirements_SWE1/node.md) | Node firmware, portable protocol, build & CI |
| [`gateway.md`](01_requirements_SWE1/gateway.md) | C++17 Linux gateway + fleet simulator |
| [`driver.md`](01_requirements_SWE1/driver.md) | Linux kernel character driver (planned) |

## Architecture (SWE.2)

| File | Contains |
|---|---|
| [`architecture.md`](02_architecture_SWE2/architecture.md) | Layering, I²C dependency injection, wire format, gateway pipeline, roadmap tiers, decision log |
| [`diagrams.md`](02_architecture_SWE2/diagrams.md) | Mermaid diagrams that render on GitHub |
| [`diagram.html`](02_architecture_SWE2/diagram.html) | One-page visual overview |
| [`gateway_design.html`](02_architecture_SWE2/gateway_design.html) | Interactive gateway pipeline canvas |

## Conventions

- Requirements use a unique ID (`REQ-GW-TR-002`), one requirement per statement,
  **SHALL** for anything mandatory, and each must be verifiable.
- Every requirement traces to an implementing artifact and a verifying test —
  see the traceability table at the bottom of each requirements file.
- Requirements state *what*, not *how*; design decisions and their rejected
  alternatives live in the architecture decision logs.
