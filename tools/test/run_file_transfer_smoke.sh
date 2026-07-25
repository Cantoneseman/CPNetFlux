#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_file_transfer_smoke.sh [options]

Options:
  --build-dir <path>   default: build
  --port-base <port>   default: 19400
USAGE
}

build_dir="build"
port_base="19400"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir="${2:-}"; shift 2 ;;
        --port-base) port_base="${2:-}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

server_bin="${build_dir%/}/gridflux-file-server"
client_bin="${build_dir%/}/gridflux-file-client"
memory_server_bin="${build_dir%/}/gridflux-server"
memory_client_bin="${build_dir%/}/gridflux-client"

for bin in "$server_bin" "$client_bin" "$memory_server_bin" "$memory_client_bin"; do
    if [[ ! -x "$bin" ]]; then
        echo "missing executable: $bin" >&2
        exit 1
    fi
done

tmp_dir="$(mktemp -d /tmp/gridflux-file-smoke.XXXXXX)"
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]] && kill -0 "$server_pid" >/dev/null 2>&1; then
        kill "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

make_file() {
    local path="$1"
    local bytes="$2"
    python3 - "$path" "$bytes" <<'PY'
import sys

path = sys.argv[1]
remaining = int(sys.argv[2])
block = bytes((index % 251 for index in range(65536)))
with open(path, "wb") as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
PY
}

run_file_case() {
    local name="$1"
    local bytes="$2"
    local connections="$3"
    local chunk_size="$4"
    local buffer_size="$5"
    local port="$6"
    local src="${tmp_dir}/${name}.src"
    local dst="${tmp_dir}/${name}.dst"
    local server_log="${tmp_dir}/${name}.server.log"

    make_file "$src" "$bytes"
    rm -f "$dst"

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections "$connections" \
        --buffer-size "$buffer_size" >"$server_log" 2>&1 &
    server_pid=$!
    sleep 0.2

    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src" \
        --connections "$connections" \
        --chunk-size "$chunk_size" \
        --buffer-size "$buffer_size"

    wait "$server_pid"
    server_pid=""
    cmp "$src" "$dst"
}

run_memory_sink_case() {
    local port="$1"
    local server_log="${tmp_dir}/memory.server.log"

    "$memory_server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --connections 2 \
        --bytes 1048576 \
        --buffer-size 65536 >"$server_log" 2>&1 &
    server_pid=$!
    sleep 0.2

    "$memory_client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --connections 2 \
        --bytes 1048576 \
        --buffer-size 65536

    wait "$server_pid"
    server_pid=""
}

run_existing_output_reject_case() {
    local dst="${tmp_dir}/existing.dst"
    local log="${tmp_dir}/existing.server.log"
    printf 'existing\n' >"$dst"

    if "$server_bin" \
        --host 127.0.0.1 \
        --port "$1" \
        --output "$dst" \
        --connections 1 \
        --buffer-size 65536 >"$log" 2>&1; then
        echo "server unexpectedly allowed existing output without --overwrite" >&2
        exit 1
    fi

    grep -q "output file already exists" "$log"
    grep -q "existing" "$dst"
}

run_file_case "empty" 0 2 1048576 65536 "$port_base"
run_file_case "small" 12345 2 1048576 65536 "$((port_base + 1))"
run_file_case "multi_16m" 16777216 4 1048576 65536 "$((port_base + 2))"
run_file_case "tail" 5255225 3 1048576 65536 "$((port_base + 3))"
run_memory_sink_case "$((port_base + 4))"
run_existing_output_reject_case "$((port_base + 5))"

echo "gridflux file transfer smoke passed"
