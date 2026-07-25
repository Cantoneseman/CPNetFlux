#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_file_checksum_private_once.sh [options]

Options:
  --remote <user@host>              default: root@<redacted>
  --server-host <ip>                default: <redacted>
  --local-build-dir <path>          default: /root/projects/GridFlux/build
  --remote-build-dir <path>         default: /root/projects/GridFlux/build
  --output-dir <path>               default: tools/perf/results
  --port <port>                     default: 19900
  --connections <N>                 default: 4
  --bytes <N>                       default: 67108864
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
port="19900"
connections="4"
bytes="67108864"
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
case_id="${timestamp}_file_checksum_private_resume_c${connections}_chunk${chunk_size}_buf${buffer_size}_bytes${bytes}_${checksum}_${checksum_backend}_p${port}"
transfer_id="phase2c-private-${timestamp}"
server_partial_log="${output_dir}/${case_id}_server_partial.log"
client_partial_log="${output_dir}/${case_id}_client_partial.log"
server_resume_log="${output_dir}/${case_id}_server_resume.log"
client_resume_log="${output_dir}/${case_id}_client_resume.log"
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
    echo "remote gridflux-file-client is missing; sync and build ${remote} first" >&2
    exit 1
fi

cleanup() {
    if [[ "$keep_files" != "true" ]]; then
        rm -f "$local_dest" "$local_dest.gridflux.manifest" "$local_dest.part.$transfer_id"
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
"

source_sha256="$("${ssh_cmd[@]}" "sha256sum '$remote_source' | awk '{print \$1}'")"
rm -f "$local_dest" "$local_dest.gridflux.manifest" "$local_dest.part.$transfer_id"

"$local_server_bin" \
    --host "$server_host" \
    --port "$port" \
    --output "$local_dest" \
    --connections "$connections" \
    --buffer-size "$buffer_size" \
    --checksum "$checksum" \
    --checksum-backend "$checksum_backend" >"$server_partial_log" 2>&1 &
server_pid=$!
sleep 1

set +e
"${ssh_cmd[@]}" "'$remote_client_bin' --host '$server_host' --port '$port' --input '$remote_source' --connections '$connections' --chunk-size '$chunk_size' --buffer-size '$buffer_size' --checksum '$checksum' --checksum-backend '$checksum_backend' --transfer-id '$transfer_id' --max-chunks 8" >"$client_partial_log" 2>&1
client_partial_status=$?
wait "$server_pid"
server_partial_status=$?
set -e

if [[ "$client_partial_status" -eq 0 || "$server_partial_status" -eq 0 ]]; then
    echo "partial checksum private transfer unexpectedly succeeded" >&2
    exit 1
fi
test ! -f "$local_dest"
test -f "$local_dest.gridflux.manifest"
test -f "$local_dest.part.$transfer_id"

"$local_server_bin" \
    --host "$server_host" \
    --port "$port" \
    --output "$local_dest" \
    --connections "$connections" \
    --buffer-size "$buffer_size" \
    --checksum "$checksum" \
    --checksum-backend "$checksum_backend" \
    --resume >"$server_resume_log" 2>&1 &
server_pid=$!
sleep 1

"${ssh_cmd[@]}" "'$remote_client_bin' --host '$server_host' --port '$port' --input '$remote_source' --connections '$connections' --chunk-size '$chunk_size' --buffer-size '$buffer_size' --checksum '$checksum' --checksum-backend '$checksum_backend' --transfer-id '$transfer_id' --resume" >"$client_resume_log" 2>&1
wait "$server_pid"

dest_sha256="$(sha256sum "$local_dest" | awk '{print $1}')"

echo "source_sha256=${source_sha256}"
echo "dest_sha256=${dest_sha256}"
echo "server_partial_log=${server_partial_log}"
echo "client_partial_log=${client_partial_log}"
echo "server_resume_log=${server_resume_log}"
echo "client_resume_log=${client_resume_log}"
echo "checksum_backend=${checksum_backend}"

if [[ "$source_sha256" != "$dest_sha256" ]]; then
    exit 1
fi

echo "gridflux private checksum resume smoke passed"
