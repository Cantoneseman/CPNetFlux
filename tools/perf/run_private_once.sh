#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_private_once.sh [options]

Options:
  --remote <user@host>          default: root@<redacted>
  --server-host <ip>            default: <redacted>
  --build-dir <path>            default: /root/projects/GridFlux/build
  --output-dir <path>           default: tools/perf/results
  --port <port>                 default: 19000
  --connections <N>             default: 8
  --bytes <N>                   default: 1073741824
  --buffer-size <N>             default: 65536
USAGE
}

remote="root@<redacted>"
server_host="<redacted>"
build_dir="/root/projects/GridFlux/build"
output_dir="tools/perf/results"
port="19000"
connections="8"
bytes="1073741824"
buffer_size="65536"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --remote) remote="${2:-}"; shift 2 ;;
        --server-host) server_host="${2:-}"; shift 2 ;;
        --build-dir) build_dir="${2:-}"; shift 2 ;;
        --output-dir) output_dir="${2:-}"; shift 2 ;;
        --port) port="${2:-}"; shift 2 ;;
        --connections) connections="${2:-}"; shift 2 ;;
        --bytes) bytes="${2:-}"; shift 2 ;;
        --buffer-size) buffer_size="${2:-}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

mkdir -p "$output_dir"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
hostname_value="$(hostname)"
case_id="${timestamp}_private_c${connections}_b${buffer_size}_bytes${bytes}_p${port}"
server_log="${output_dir}/${case_id}_server.log"
client_log="${output_dir}/${case_id}_client.log"
csv_path="${output_dir}/${case_id}.csv"

server_bin="${build_dir%/}/gridflux-server"
remote_client_bin="${build_dir%/}/gridflux-client"

if [[ ! -x "$server_bin" ]]; then
    echo "missing executable: ${server_bin}" >&2
    exit 1
fi

ssh_cmd=(ssh -o StrictHostKeyChecking=no "$remote")
if [[ -n "${GRIDFLUX_SSH_PASSWORD:-}" ]]; then
    if ! command -v sshpass >/dev/null 2>&1; then
        echo "sshpass is required when GRIDFLUX_SSH_PASSWORD is set" >&2
        exit 1
    fi
    ssh_cmd=(sshpass -e ssh -o StrictHostKeyChecking=no "$remote")
    export SSHPASS="$GRIDFLUX_SSH_PASSWORD"
fi

if ! "${ssh_cmd[@]}" "test -x '$remote_client_bin'"; then
    echo "remote gridflux-client is missing; run sync_remote.sh and build on ${remote}" >&2
    exit 1
fi

"$server_bin" \
    --host "$server_host" \
    --port "$port" \
    --connections "$connections" \
    --bytes "$bytes" \
    --buffer-size "$buffer_size" >"$server_log" 2>&1 &
server_pid=$!

sleep 1

set +e
"${ssh_cmd[@]}" "'$remote_client_bin' --host '$server_host' --port '$port' --connections '$connections' --bytes '$bytes' --buffer-size '$buffer_size'" >"$client_log" 2>&1
client_status=$?
wait "$server_pid"
server_status=$?
set -e

extract_metric() {
    local role="$1"
    local file="$2"
    local key="$3"
    python3 - "$role" "$file" "$key" <<'PY'
import re
import sys

role, path, key = sys.argv[1], sys.argv[2], sys.argv[3]
pattern = re.compile(
    rf"{role} (?P<byte_key>sent_bytes|received_bytes)=(?P<bytes>\d+) "
    rf"elapsed_seconds=(?P<elapsed>[0-9.]+) throughput_gbps=(?P<gbps>[0-9.]+)"
)
text = open(path, encoding="utf-8").read()
for line in text.splitlines():
    match = pattern.search(line.strip())
    if match:
        print(match.group(key))
        sys.exit(0)
print("")
PY
}

client_bytes="$(extract_metric client "$client_log" bytes)"
client_elapsed="$(extract_metric client "$client_log" elapsed)"
client_gbps="$(extract_metric client "$client_log" gbps)"
server_bytes="$(extract_metric server "$server_log" bytes)"
server_elapsed="$(extract_metric server "$server_log" elapsed)"
server_gbps="$(extract_metric server "$server_log" gbps)"
result="pass"
if [[ "$client_status" -ne 0 || "$server_status" -ne 0 ]]; then
    result="fail"
fi

{
    echo "timestamp,hostname,mode,host,port,connections,buffer_size,bytes,client_elapsed_seconds,client_throughput_gbps,server_elapsed_seconds,server_throughput_gbps,client_bytes,server_bytes,result"
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ),${hostname_value},private,${server_host},${port},${connections},${buffer_size},${bytes},${client_elapsed},${client_gbps},${server_elapsed},${server_gbps},${client_bytes},${server_bytes},${result}"
} >"$csv_path"

echo "wrote ${csv_path}"
echo "server log: ${server_log}"
echo "client log: ${client_log}"

if [[ "$result" != "pass" ]]; then
    exit 1
fi
