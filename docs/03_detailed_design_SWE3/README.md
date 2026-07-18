# Detailed design (SWE.3)

Per-unit internals: data structures, algorithms, state machines, buffer
ownership, and the concurrency and error handling each unit implements.

Where SWE.2 says *which component does it*, SWE.3 says *how that component
works inside* — and it is what unit tests (SWE.4) are written against.

| File | Component | Status |
|---|---|---|
| `gateway.md` | C++17 Linux gateway, per stage | pending |
| `node.md` | Node firmware units | pending |

Open questions this layer must settle for the gateway (tracked in
[`../01_requirements_SWE1/gateway.md`](../01_requirements_SWE1/gateway.md) §2–§4
as TBD items):

- per-node parser buffer size and ownership
- event-loop structure (`epoll`) and connection lifecycle
- Store record schema and write path
- what each error category does at each stage
