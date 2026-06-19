#!/bin/sh
# =============================================================================
# run_qemu.sh — boot the Sharak node image under QEMU and expose its UART0
#               as a TCP socket you can read with nc/xxd.
#
# WHY this exists
#   The node firmware talks to the outside world over UART0. QEMU's
#   'lm3s6965evb' machine faithfully emulates that UART (it does NOT emulate
#   I2C — that is exactly why the sensor is the injected i2c_sim under
#   emulation; see docs/architecture.md). So "run the firmware" really means:
#   start QEMU, route the emulated UART to a socket, and watch the bytes.
#
#   The bytes are NOT text. main.c emits HDLC-framed binary: each frame is
#   0x7E <stuffed 16-byte payload + 2-byte CRC> 0x7E. So we pipe through xxd
#   to see the structure (you will spot the 7E flags and the 01 01 header).
#
# USAGE
#   1) Build first:        make firmware
#   2) Start the node:     scripts/run_qemu.sh
#   3) In another shell:   nc 127.0.0.1 5555 | xxd | head
#
#   The banner line "Sharak node online\r\n" is plain ASCII and appears once,
#   before the first binary frame — handy proof the image actually booted.
#
# REQUIREMENTS
#   qemu-system-arm (the 'lm3s6965evb' machine ships with mainline QEMU).
#
# Ctrl-A then X exits QEMU when running with -nographic.
# =============================================================================
set -eu

BIN="${BIN:-build/sharak_node.bin}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-5555}"

if [ ! -f "$BIN" ]; then
    echo "error: $BIN not found — run 'make firmware' first" >&2
    exit 1
fi

if ! command -v qemu-system-arm >/dev/null 2>&1; then
    echo "error: qemu-system-arm not found on PATH" >&2
    echo "       install QEMU (it provides the lm3s6965evb machine)." >&2
    exit 1
fi

echo "Sharak node booting under QEMU; UART0 -> tcp://$HOST:$PORT"
echo "Read it from another terminal:  nc $HOST $PORT | xxd | head"

# -M lm3s6965evb : the Stellaris board we linked for.
# -nographic     : no GUI; we only care about the serial line.
# -serial tcp:... ,server,nowait : publish UART0 as a TCP server and DON'T block
#                  waiting for a client, so the firmware runs even if nobody has
#                  connected yet (bytes emitted before connect are simply lost).
exec qemu-system-arm \
    -M lm3s6965evb \
    -nographic \
    -kernel "$BIN" \
    -serial "tcp:$HOST:$PORT,server,nowait"
