# Sharak Linux driver

A Linux **character-device driver** that exposes a virtual Sharak sensor to
user space as `/dev/sharak0`.

Planned scope:

- `open` / `release` / `read` / `write` file operations
- `read()` returns the latest telemetry payload (same 16-byte format as the
  on-wire payload defined in `../protocol`)
- `write()` lets user space inject a simulated reading for testing
- builds as an out-of-tree module against the running kernel's headers
- a `demo.sh` that loads the module, reads from the device, and unloads it

This layer extends Sharak into kernel space, exposing telemetry through a
standard Linux device-file interface. It builds on the node firmware, protocol,
and gateway, which provide the validated data path it surfaces.

> Building kernel modules requires `linux-headers-$(uname -r)` and root to
> `insmod`/`rmmod`. CI will compile the module against kernel headers but will
> not load it.
