# Sharak Learning Tracker (LOCAL-ONLY, git-ignored)

Legend: ✅ done · 🔄 in progress · 🔁 needs review/redo · ⬜ not started

---

## M3 — Linux Gateway (C++17)

| Day | Lesson | Topic | Sharak artifact | Status |
|-----|--------|-------|------------------|--------|
| 2  | Tour  | Defend the node (startup, linker, I2C DI, CRC FSM)        | (reasoning only)            | 🔄 |
| 3  | C3.1  | C→C++ crossing: TUs, linkage, `extern "C"`, refs, namespaces, CMake | `protocol.h` guard + build | 🔄 |
| 4  | C3.2  | classes, ctor/dtor, RAII                                  | core gateway types          | ⬜ |
| 5  | C3.3  | `enum class`, `std::optional`, errors without exceptions | error model                 | ⬜ |
| 6  | C3.4  | `std::byte`, `std::span`; FrameParser via TDD            | `FrameParser`               | ⬜ |
| 7  | C3.5  | ownership: `unique_ptr`/`shared_ptr`, move, rule 0/3/5    | ownership pass              | ⬜ |
| 8  | C3.6  | `string_view`, containers, STL algorithms                | `Store`                     | ⬜ |
| 9  | C3.7  | Linux I/O: fds, termios, RAII fd wrapper, TCP transport  | live connect to QEMU node   | ⬜ |
| 10 | C3.8  | Store + dependency injection; integration                | **M3 MILESTONE**            | ⬜ |

## M4 — Linux Kernel Driver (C)

| Day | Lesson | Topic | Sharak artifact | Status |
|-----|--------|-------|------------------|--------|
| 11 | C4.1–2 | kernel modules, Kbuild, char devices, `file_operations` | module skeleton            | ⬜ |
| 12 | C4.3   | open/read/write/release, `copy_to_user`                 | `/dev/sharak0` v1         | ⬜ |
| 13 | C4.4–5 | mutex/spinlock/waitqueue + end-to-end                   | **M4 MILESTONE**           | ⬜ |
| 14 | Polish | README, CI green, interview battle-card, tag v1.0       | `v1.0`                     | ⬜ |

---

## Milestone roll-up
- **M0/M1/M2 — Node firmware + protocol:** ✅ (host tests green; reference material)
- **M3 — Gateway:** 🔄 (Day 3 — C3.1 C→C++ crossing in progress)
- **M4 — Kernel driver:** ⬜

## Design artifacts (2026-06-17)
- `docs/gateway_design.html` — interactive gateway pipeline design canvas (5 stages behind interfaces, wire-contract firewall, extern "C" seam).
- `../reference/schematics/SHARAK_Schematic.pdf` + `../reference/schematics/schematic_node.svg` + `../reference/schematics/schematic_system_stages.svg` — component-level node schematic + system-stages sheet, drawn from the firmware pin map.
