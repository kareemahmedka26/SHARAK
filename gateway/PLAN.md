# Gateway build plan — 10 commits

C++17 Linux gateway, built one stage at a time. Every commit ships with its own
tests; testing is not a phase at the end.

| # | Date | Commit | Delivers |
|---|------|--------|----------|
| 1 | Sat Jul 18 | skeleton | CMake build, test harness, one passing smoke test |
| 2 | Sun Jul 19 | decode seam | `extern "C"` wrapper over `protocol.c`, dispatch on `type` |
| 3 | Mon Jul 20 | error model | `enum class` status + `Telemetry` value types |
| 4 | Tue Jul 21 | frame parser | streaming HDLC reassembly, resync on corruption |
| 5 | Wed Jul 22 | store interface | abstract `Store` + in-memory backend |
| 6 | Thu Jul 23 | tcp transport | POSIX sockets, single node ingest |
| 7 | Fri Jul 24 | multi-node | concurrent fleet, identity derived from transport |
| 8 | Sat Jul 25 | sqlite store | `Store` backed by SQLite |
| 9 | Sun Jul 26 | exposure CLI | query last-N + per-node stats; MQTT publisher stubbed |
| 10 | Mon Jul 27 | integration | end-to-end against the fleet simulator + README |

## Rules

- **One stage per commit.** Each stage sits behind an interface so the next can
  replace or fake it.
- **Tests land with the code**, never after.
- **No second decoder.** The wire format has exactly one implementation
  (`protocol.c`), linked into C++ through `extern "C"`.
- **Node identity comes from the transport**, not the payload.
- Build stays warning-clean (`-Wall -Wextra -Werror`).

## Scope

Aug-1 target is this gateway MVP. The cloud dashboard and the on-gateway
edge-AI stage are roadmap (see `docs/architecture.md` §10) — the MQTT/cloud
seam exists in commit 9, the implementation does not.
