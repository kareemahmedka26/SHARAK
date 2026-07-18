# Sharak — Requirements (Linux kernel character driver)

Style: ASPICE SWE.1-flavored numbered requirements, same as the other
requirement docs. **Status: planned** — this specifies the kernel stage before
it is implemented; the traceability cells read `(pending)`.

Scope: a Linux **character device** that exposes a Sharak virtual sensor as
`/dev/sharak0`, built against the kernel headers as an out-of-tree module. It is
the M4 stage, after the gateway (M3). Kernel C is **not** userspace C: no libc,
explicit execution contexts, controlled sleeping, and disciplined user/kernel
data copies — every such difference is called out where it applies.

---

## 1. Module & build (REQ-DRV-MOD)

- **MOD-001** The driver SHALL build as an out-of-tree loadable kernel module via
  Kbuild against the running kernel's headers (`/lib/modules/$(uname -r)/build`).
- **MOD-002** It SHALL register and unregister cleanly on `insmod`/`rmmod` with no
  leaked resources, and SHALL declare `MODULE_LICENSE`, author, and description.
- **MOD-003** It SHALL log init/exit via the kernel log (`pr_info`), not stdio
  (no libc in kernel space).

## 2. Character device (REQ-DRV-CDEV)

- **CDEV-001** The driver SHALL create a character device `/dev/sharak0` with a
  dynamically allocated major number and a `struct file_operations` table.
- **CDEV-002** It SHALL implement `open`, `read`, `write`, and `release`.
- **CDEV-003** `read` SHALL return telemetry bytes to user space exclusively via
  `copy_to_user`, and `write` SHALL ingest via `copy_from_user`, with the return
  value checked (a non-zero result means bytes were not copied → `-EFAULT`).
- **CDEV-004** It SHALL never dereference a user-supplied pointer directly.

## 3. Concurrency & blocking (REQ-DRV-SYNC)

- **SYNC-001** Shared driver state SHALL be protected appropriately for its
  context: a mutex where the holder may sleep, a spinlock where it may not.
- **SYNC-002** A blocking `read` on no-data SHALL sleep on a wait queue and wake
  on new data, and SHALL honour `O_NONBLOCK` by returning `-EAGAIN`.
- **SYNC-003** Sleeping SHALL never occur while holding a spinlock or in any
  atomic context.

## 4. End-to-end & verification (REQ-DRV-E2E / REQ-DRV-TS)

- **E2E-001** Telemetry written by the gateway (or a test) SHALL be readable back
  through `/dev/sharak0`, preserving byte content and order.
- **TS-001** A user-space test script SHALL load the module, exercise
  open/write/read/release (including `O_NONBLOCK` and a blocking-read wake), and
  unload it, failing on any unexpected result.
- **TS-002** Build SHALL be warning-clean; the module SHALL load and unload with
  no kernel warnings/oopses in `dmesg`.

---

## Traceability

| Requirement | Implemented in | Verified by |
|---|---|---|
| REQ-DRV-MOD-001..003 | (pending) | (pending) |
| REQ-DRV-CDEV-001..004 | (pending) | (pending) |
| REQ-DRV-SYNC-001..003 | (pending) | (pending) |
| REQ-DRV-E2E-001 | (pending) | (pending) |
| REQ-DRV-TS-001..002 | (pending) | (pending) |
