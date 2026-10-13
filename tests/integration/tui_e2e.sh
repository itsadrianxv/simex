#!/usr/bin/env bash
set -euo pipefail

server="$1"
tui="$2"
root="$3"
port_tcp=29101
port_udp=29102
tmp="$(mktemp -d)"
fifo="$tmp/input"
server_log="$tmp/server.log"
tui_log="$tmp/tui.log"
cleanup() {
  exec 3>&- 2>/dev/null || true
  kill "${tui_pid:-}" "${server_pid:-}" 2>/dev/null || true
  wait "${tui_pid:-}" 2>/dev/null || true
  wait "${server_pid:-}" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT

cat >"$tmp/server.json" <<EOF
{
  "instrument_config": "$root/simex.json",
  "reference_price": 3500,
  "trading_day": 20260926,
  "client_id": 1,
  "tcp_port": $port_tcp,
  "udp_destination_port": $port_udp,
  "snapshot_interval_ms": 100,
  "queue_capacity": 4096,
  "max_run_seconds": 30,
  "phase_override": "CONTINUOUS",
  "participant_simulator": {"enabled": false}
}
EOF

"$server" "$tmp/server.json" >"$server_log" 2>&1 &
server_pid=$!
for _ in $(seq 1 100); do
  grep -q 'event=simex_ready' "$server_log" && break
  sleep 0.05
done
grep -q 'event=simex_ready' "$server_log"

mkfifo "$fifo"
"$tui" "$port_udp" --plain <"$fifo" >"$tui_log" 2>&1 &
tui_pid=$!
exec 3>"$fifo"
sleep 0.5

python3 - "$port_tcp" <<'PY'
import socket, struct, sys, time
port = int(sys.argv[1])
def request(sequence, order_id, side, price):
    header = struct.pack('<HBBIQ', 1, 1, 0, 56, sequence)
    payload = struct.pack('@B3xIIQbbbb4xqI4xq', 0, 1, 0, order_id, side, 0, 0, 0, price, 1, 0)
    return header + payload
sock = socket.create_connection(('127.0.0.1', port), timeout=3)
sock.sendall(request(0, 100, -1, 3500))
sock.sendall(request(1, 101, 1, 3500))
time.sleep(0.5)
sock.close()
PY

sleep 0.5
printf q >&3
wait "$tui_pid"
grep -q 'state=SYNCED' "$tui_log"
grep -q 'event=ADD' "$tui_log"
grep -q 'event=TRADE' "$tui_log"
grep -q 'event=CANCEL' "$tui_log"
