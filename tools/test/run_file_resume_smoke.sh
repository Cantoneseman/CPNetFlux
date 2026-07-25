#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_file_resume_smoke.sh [options]

Options:
  --build-dir <path>   default: build
  --port-base <port>   default: 19700
USAGE
}

build_dir="build"
port_base="19700"

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

for bin in "$server_bin" "$client_bin"; do
    if [[ ! -x "$bin" ]]; then
        echo "missing executable: $bin" >&2
        exit 1
    fi
done

tmp_dir="$(mktemp -d /tmp/gridflux-file-resume.XXXXXX)"
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

run_partial_then_resume() {
    local src="${tmp_dir}/resume.src"
    local dst="${tmp_dir}/resume.dst"
    local transfer_id="phase2a-smoke"
    local port="$port_base"
    make_file "$src" 67108864
    rm -f "$dst" "$dst.gridflux.manifest" "$dst.part.$transfer_id"

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections 4 \
        --buffer-size 65536 >"${tmp_dir}/partial.server.log" 2>&1 &
    server_pid=$!
    sleep 0.3

    set +e
    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src" \
        --connections 4 \
        --chunk-size 1048576 \
        --buffer-size 65536 \
        --transfer-id "$transfer_id" \
        --max-chunks 8 >"${tmp_dir}/partial.client.log" 2>&1
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""

    if [[ "$client_status" -eq 0 || "$server_status" -eq 0 ]]; then
        echo "partial transfer unexpectedly succeeded" >&2
        exit 1
    fi
    test ! -f "$dst"
    test -f "$dst.gridflux.manifest"
    test -f "$dst.part.$transfer_id"

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections 4 \
        --buffer-size 65536 \
        --resume >"${tmp_dir}/resume.server.log" 2>&1 &
    server_pid=$!
    sleep 0.3

    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src" \
        --connections 4 \
        --chunk-size 1048576 \
        --buffer-size 65536 \
        --transfer-id "$transfer_id" \
        --resume >"${tmp_dir}/resume.client.log" 2>&1

    wait "$server_pid"
    server_pid=""
    cmp "$src" "$dst"
}

run_corrupt_manifest_case() {
    local src="${tmp_dir}/corrupt.src"
    local dst="${tmp_dir}/corrupt.dst"
    local transfer_id="phase2a-corrupt"
    local port="$((port_base + 1))"
    make_file "$src" 1048576
    printf 'not-a-manifest\n' >"$dst.gridflux.manifest"

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections 1 \
        --buffer-size 65536 \
        --resume >"${tmp_dir}/corrupt.server.log" 2>&1 &
    server_pid=$!
    sleep 0.3

    set +e
    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src" \
        --connections 1 \
        --chunk-size 1048576 \
        --buffer-size 65536 \
        --transfer-id "$transfer_id" \
        --resume >"${tmp_dir}/corrupt.client.log" 2>&1
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""

    if [[ "$client_status" -eq 0 || "$server_status" -eq 0 ]]; then
        echo "corrupt manifest resume unexpectedly succeeded" >&2
        exit 1
    fi
}

run_size_mismatch_case() {
    local src_a="${tmp_dir}/mismatch-a.src"
    local src_b="${tmp_dir}/mismatch-b.src"
    local dst="${tmp_dir}/mismatch.dst"
    local transfer_id="phase2a-mismatch"
    local port="$((port_base + 2))"
    make_file "$src_a" 4194304
    make_file "$src_b" 5242880

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections 1 \
        --buffer-size 65536 >"${tmp_dir}/mismatch-partial.server.log" 2>&1 &
    server_pid=$!
    sleep 0.3

    set +e
    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src_a" \
        --connections 1 \
        --chunk-size 1048576 \
        --buffer-size 65536 \
        --transfer-id "$transfer_id" \
        --max-chunks 1 >"${tmp_dir}/mismatch-partial.client.log" 2>&1
    wait "$server_pid"
    set -e
    server_pid=""

    "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --output "$dst" \
        --connections 1 \
        --buffer-size 65536 \
        --resume >"${tmp_dir}/mismatch-resume.server.log" 2>&1 &
    server_pid=$!
    sleep 0.3

    set +e
    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src_b" \
        --connections 1 \
        --chunk-size 1048576 \
        --buffer-size 65536 \
        --transfer-id "$transfer_id" \
        --resume >"${tmp_dir}/mismatch-resume.client.log" 2>&1
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""

    if [[ "$client_status" -eq 0 || "$server_status" -eq 0 ]]; then
        echo "size mismatch resume unexpectedly succeeded" >&2
        exit 1
    fi
}

run_partial_then_resume
run_corrupt_manifest_case
run_size_mismatch_case

echo "gridflux file resume smoke passed"
