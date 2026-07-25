#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_file_private_once.sh [options]

Options:
  --remote <user@host>              default: root@<redacted>
  --server-host <ip>                default: <redacted>
  --local-build-dir <path>          default: /root/projects/GridFlux/build
  --remote-build-dir <path>         default: /root/projects/GridFlux/build
  --output-dir <path>               default: tools/perf/results
  --port <port>                     default: 19600
  --connections <N>                 default: 4
  --bytes <N>                       default: 268435456
  --chunk-size <N>                  default: 1048576
  --buffer-size <N>                 default: 65536
  --checksum <crc32c|none>          default: crc32c
  --checksum-backend <auto|software|hardware>
                                    default: auto
  --keep-files                      keep generated source/destination files
USAGE
}

remote="root@<redacted>"
server_host="<redacted>"
local_build_dir="/root/projects/GridFlux/build"
remote_build_dir="/root/projects/GridFlux/build"
output_dir="tools/perf/results"
port="19600"
connections="4"
bytes="268435456"
chunk_size="1048576"
buffer_size="65536"
checksum="crc32c"
checksum_backend="auto"
keep_files="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --remote) remote="${2:-}"; shift 2 ;;
        --server-host) server_host="${2:-}"; shift 2 ;;
        --local-build-dir) local_build_dir="${2:-}"; shift 2 ;;
        --remote-build-dir) remote_build_dir="${2:-}"; shift 2 ;;
        --output-dir) output_dir="${2:-}"; shift 2 ;;
        --port) port="${2:-}"; shift 2 ;;
        --connections) connections="${2:-}"; shift 2 ;;
        --bytes) bytes="${2:-}"; shift 2 ;;
        --chunk-size) chunk_size="${2:-}"; shift 2 ;;
        --buffer-size) buffer_size="${2:-}"; shift 2 ;;
        --checksum) checksum="${2:-}"; shift 2 ;;
        --checksum-backend) checksum_backend="${2:-}"; shift 2 ;;
        --keep-files) keep_files="true"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ "$checksum" != "crc32c" && "$checksum" != "none" ]]; then
    echo "--checksum must be crc32c or none" >&2
    exit 2
fi
if [[ "$checksum_backend" != "auto" && "$checksum_backend" != "software" && "$checksum_backend" != "hardware" ]]; then
    echo "--checksum-backend must be auto, software, or hardware" >&2
    exit 2
fi

mkdir -p "$output_dir"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
case_id="${timestamp}_file_private_c${connections}_chunk${chunk_size}_buf${buffer_size}_bytes${bytes}_${checksum}_${checksum_backend}_p${port}"
server_log="${output_dir}/${case_id}_server.log"
client_log="${output_dir}/${case_id}_client.log"
csv_path="${output_dir}/${case_id}.csv"
local_server_bin="${local_build_dir%/}/gridflux-file-server"
remote_client_bin="${remote_build_dir%/}/gridflux-file-client"
remote_source="/tmp/${case_id}.src"
local_dest="/tmp/${case_id}.dst"

if [[ ! -x "$local_server_bin" ]]; then
    echo "missing executable: ${local_server_bin}" >&2
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
    echo "remote gridflux-file-client is missing; run sync_remote.sh and build on ${remote}" >&2
    exit 1
fi

cleanup() {
    if [[ "$keep_files" != "true" ]]; then
        rm -f "$local_dest"
        "${ssh_cmd[@]}" "rm -f '$remote_source'" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

"${ssh_cmd[@]}" "python3 - '$remote_source' '$bytes' <<'PY'
import sys

path = sys.argv[1]
remaining = int(sys.argv[2])
block = bytes(index % 251 for index in range(1024 * 1024))
with open(path, 'wb') as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
PY
sha256sum '$remote_source'"

source_sha256="$("${ssh_cmd[@]}" "sha256sum '$remote_source' | awk '{print \$1}'")"

rm -f "$local_dest"
"$local_server_bin" \
    --host "$server_host" \
    --port "$port" \
    --output "$local_dest" \
    --connections "$connections" \
    --buffer-size "$buffer_size" \
    --checksum "$checksum" \
    --checksum-backend "$checksum_backend" >"$server_log" 2>&1 &
server_pid=$!

sleep 1

set +e
"${ssh_cmd[@]}" "'$remote_client_bin' --host '$server_host' --port '$port' --input '$remote_source' --connections '$connections' --chunk-size '$chunk_size' --buffer-size '$buffer_size' --checksum '$checksum' --checksum-backend '$checksum_backend'" >"$client_log" 2>&1
client_status=$?
wait "$server_pid"
server_status=$?
set -e

dest_sha256=""
if [[ -f "$local_dest" ]]; then
    dest_sha256="$(sha256sum "$local_dest" | awk '{print $1}')"
fi

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
key_pattern = re.compile(rf"(?:^|\s){re.escape(key)}=([^\s]+)")
text = open(path, encoding="utf-8").read()
for line in text.splitlines():
    stripped = line.strip()
    if not stripped.startswith(role + " "):
        continue
    if key in {"elapsed", "gbps", "bytes"}:
        match = pattern.search(stripped)
        if match:
            print(match.group(key))
            sys.exit(0)
    key_match = key_pattern.search(stripped)
    if key_match:
        print(key_match.group(1))
        sys.exit(0)
print("")
PY
}

client_elapsed="$(extract_metric file_client "$client_log" elapsed)"
client_gbps="$(extract_metric file_client "$client_log" gbps)"
server_elapsed="$(extract_metric file_server "$server_log" elapsed)"
server_gbps="$(extract_metric file_server "$server_log" gbps)"
actual_checksum_backend="$(extract_metric file_client "$client_log" checksum_backend)"
if [[ -z "$actual_checksum_backend" ]]; then
    actual_checksum_backend="$(extract_metric file_server "$server_log" checksum_backend)"
fi
if [[ -z "$actual_checksum_backend" ]]; then
    actual_checksum_backend="$checksum_backend"
fi
skipped_bytes="$(extract_metric file_client "$client_log" skipped_bytes)"
resent_bytes="$(extract_metric file_client "$client_log" resent_bytes)"
verified_bytes="$(extract_metric file_client "$client_log" verified_bytes)"
manifest_flush_policy="$(extract_metric file_server "$server_log" manifest_flush_policy)"
manifest_flush_count="$(extract_metric file_server "$server_log" manifest_flush_count)"
elapsed="${server_elapsed:-$client_elapsed}"
throughput_gbps="${server_gbps:-$client_gbps}"
result="pass"
if [[ "$client_status" -ne 0 || "$server_status" -ne 0 || "$source_sha256" != "$dest_sha256" ]]; then
    result="fail"
fi

{
    echo "timestamp,mode,host,port,connections,chunk_size,buffer_size,bytes,checksum_enabled,checksum_algorithm,checksum_backend,skipped_bytes,resent_bytes,verified_bytes,manifest_flush_policy,manifest_flush_count,elapsed,throughput_gbps,client_elapsed_seconds,client_throughput_gbps,server_elapsed_seconds,server_throughput_gbps,source_sha256,dest_sha256,result,server_log,client_log"
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ),file-private,${server_host},${port},${connections},${chunk_size},${buffer_size},${bytes},$([[ "$checksum" == "none" ]] && echo false || echo true),${checksum},${actual_checksum_backend},${skipped_bytes},${resent_bytes},${verified_bytes},${manifest_flush_policy},${manifest_flush_count},${elapsed},${throughput_gbps},${client_elapsed},${client_gbps},${server_elapsed},${server_gbps},${source_sha256},${dest_sha256},${result},${server_log},${client_log}"
} >"$csv_path"

echo "wrote ${csv_path}"
echo "server log: ${server_log}"
echo "client log: ${client_log}"
echo "source_sha256=${source_sha256}"
echo "dest_sha256=${dest_sha256}"

if [[ "$result" != "pass" ]]; then
    exit 1
fi
