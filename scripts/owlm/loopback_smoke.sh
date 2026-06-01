#!/usr/bin/env bash
# owlm loopback smoke test.
#
# Runs two owlm_tool instances on the loopback interface for ~3 s and
# confirms they exchanged at least 100 packets each. Both processes
# need a real gPTP-capable interface for SlaveSession::start() to
# succeed, so this script SKIPS itself with exit 77 if the
# OWLM_TEST_IFACE environment variable is unset.
#
# Usage:
#   scripts/owlm/loopback_smoke.sh <owlm_tool_binary> [<gptp_iface>]
#
# Or (when invoked from CTest):
#   OWLM_TEST_IFACE=eth0 ctest -R owlm_loopback_smoke

set -euo pipefail

BIN="${1:-./build/build-Debug/statusbar/owlm/owlm_tool}"
IFACE="${2:-${OWLM_TEST_IFACE:-}}"

if [[ -z "$IFACE" ]]; then
    echo "skip: gPTP interface not provided (set OWLM_TEST_IFACE or pass argv[2])"
    exit 77
fi

if [[ ! -x "$BIN" ]]; then
    echo "skip: binary not found or not executable: $BIN"
    exit 77
fi

PORT_A=19991
PORT_B=19992
LOG_A=$(mktemp)
LOG_B=$(mktemp)

cleanup() {
    [[ -n "${PID_A:-}" ]] && kill "$PID_A" 2>/dev/null || true
    [[ -n "${PID_B:-}" ]] && kill "$PID_B" 2>/dev/null || true
    rm -f "$LOG_A" "$LOG_B"
}
trap cleanup EXIT

# A → B
"$BIN" --gptp-interface="$IFACE" --owlm-interface=lo \
       --peer=127.0.0.1 --peer-port="$PORT_B" --local-port="$PORT_A" \
       --tx-interval=10000 --report-interval=500000 --id-mid=0xFF01 --no-redundant \
       > "$LOG_A" 2>&1 &
PID_A=$!

# B → A
"$BIN" --gptp-interface="$IFACE" --owlm-interface=lo \
       --peer=127.0.0.1 --peer-port="$PORT_A" --local-port="$PORT_B" \
       --tx-interval=10000 --report-interval=500000 --id-mid=0xFF02 --no-redundant \
       > "$LOG_B" 2>&1 &
PID_B=$!

sleep 3

kill -INT "$PID_A" "$PID_B" 2>/dev/null || true
wait "$PID_A" 2>/dev/null || true
wait "$PID_B" 2>/dev/null || true

# Each side should have received at least 100 packets in 3 s @ 100 Hz.
COUNT_A=$(grep -oE 'count=[0-9]+' "$LOG_A" | tail -1 | cut -d= -f2 || echo 0)
COUNT_B=$(grep -oE 'count=[0-9]+' "$LOG_B" | tail -1 | cut -d= -f2 || echo 0)

echo "A received: ${COUNT_A:-0} packets"
echo "B received: ${COUNT_B:-0} packets"

if [[ "${COUNT_A:-0}" -lt 100 || "${COUNT_B:-0}" -lt 100 ]]; then
    echo "FAIL: low packet counts"
    echo "--- A log ---"
    cat "$LOG_A"
    echo "--- B log ---"
    cat "$LOG_B"
    exit 1
fi

echo "PASS"
