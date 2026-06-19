#!/usr/bin/env bash
# run_fleet.sh — launch N Sharak fleet-node simulators on consecutive ports to
# present a multi-node load to the gateway. Distinct ports = distinct identities.
#
#   Usage: scripts/run_fleet.sh [N] [BASE_PORT] [RATE_HZ]   (defaults: 3 5555 100)
#   Read one node:  nc 127.0.0.1 5555 | xxd | head
set -euo pipefail

N="${1:-3}"; BASE="${2:-5555}"; RATE="${3:-100}"
BIN="$(cd "$(dirname "$0")/.." && pwd)/build/sim/fleet_node"
[ -x "$BIN" ] || { echo "simulator not built — run: make sim" >&2; exit 1; }

pids=()
cleanup() { echo; echo "stopping fleet..."; for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup INT TERM EXIT

for i in $(seq 0 $((N - 1))); do
    port=$((BASE + i))
    "$BIN" --port "$port" --rate "$RATE" &
    pids+=($!)
    echo "node $i -> 127.0.0.1:$port"
done
echo "running $N fleet nodes at ${RATE} Hz; Ctrl-C to stop."
wait
