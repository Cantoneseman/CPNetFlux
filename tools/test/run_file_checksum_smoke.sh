#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: run_file_checksum_smoke.sh [options]

Options:
  --build-dir <path>   default: build
  --port-base <port>   default: 19800
USAGE
}

build_dir="build"
port_base="19800"

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

tmp_dir="$(mktemp -d /tmp/gridflux-file-checksum.XXXXXX)"
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

flip_byte() {
    local path="$1"
    local offset="$2"
    python3 - "$path" "$offset" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
offset = int(sys.argv[2])
with path.open("r+b") as handle:
    handle.seek(offset)
    value = handle.read(1)
    if not value:
        raise SystemExit("cannot corrupt empty file")
    handle.seek(offset)
    handle.write(bytes([value[0] ^ 0xFF]))
PY
}

start_server() {
    local dst="$1"
    local port="$2"
    local connections="$3"
    local buffer_size="$4"
    local checksum="$5"
    local resume="${6:-false}"
    local log="$7"

    local args=(
        "$server_bin"
        --host 127.0.0.1
        --port "$port"
        --output "$dst"
        --connections "$connections"
        --buffer-size "$buffer_size"
        --checksum "$checksum"
    )
    if [[ "$resume" == "true" ]]; then
        args+=(--resume)
    fi

    "${args[@]}" >"$log" 2>&1 &
    server_pid=$!
    sleep 0.3
}

run_client() {
    local src="$1"
    local port="$2"
    local connections="$3"
    local chunk_size="$4"
    local buffer_size="$5"
    local checksum="$6"
    local log="$7"
    shift 7

    "$client_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --input "$src" \
        --connections "$connections" \
        --chunk-size "$chunk_size" \
        --buffer-size "$buffer_size" \
        --checksum "$checksum" \
        "$@" >"$log" 2>&1
}

expect_failed_transfer() {
    local client_status="$1"
    local server_status="$2"
    local dst="$3"
    local label="$4"
    if [[ "$client_status" -eq 0 || "$server_status" -eq 0 ]]; then
        echo "${label} unexpectedly succeeded" >&2
        exit 1
    fi
    test ! -f "$dst"
}

run_success_case() {
    local name="$1"
    local bytes="$2"
    local connections="$3"
    local checksum="$4"
    local port="$5"
    local src="${tmp_dir}/${name}.src"
    local dst="${tmp_dir}/${name}.dst"
    make_file "$src" "$bytes"
    rm -f "$dst" "$dst.gridflux.manifest" "$dst".part.*

    start_server "$dst" "$port" "$connections" 65536 "$checksum" false \
        "${tmp_dir}/${name}.server.log"
    run_client "$src" "$port" "$connections" 1048576 65536 "$checksum" \
        "${tmp_dir}/${name}.client.log"
    wait "$server_pid"
    server_pid=""
    cmp "$src" "$dst"
}

run_partial_resume_case() {
    local src="${tmp_dir}/resume.src"
    local dst="${tmp_dir}/resume.dst"
    local transfer_id="phase2b-resume"
    local port="$1"
    make_file "$src" 67108864
    rm -f "$dst" "$dst.gridflux.manifest" "$dst.part.$transfer_id"

    start_server "$dst" "$port" 4 65536 crc32c false "${tmp_dir}/resume-partial.server.log"
    set +e
    run_client "$src" "$port" 4 1048576 65536 crc32c \
        "${tmp_dir}/resume-partial.client.log" --transfer-id "$transfer_id" --max-chunks 8
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""
    expect_failed_transfer "$client_status" "$server_status" "$dst" "partial resume setup"
    test -f "$dst.gridflux.manifest"
    test -f "$dst.part.$transfer_id"

    start_server "$dst" "$port" 4 65536 crc32c true "${tmp_dir}/resume.server.log"
    run_client "$src" "$port" 4 1048576 65536 crc32c "${tmp_dir}/resume.client.log" \
        --transfer-id "$transfer_id" --resume
    wait "$server_pid"
    server_pid=""
    cmp "$src" "$dst"
}

run_temp_corruption_case() {
    local src="${tmp_dir}/temp-corrupt.src"
    local dst="${tmp_dir}/temp-corrupt.dst"
    local transfer_id="phase2b-temp-corrupt"
    local port="$1"
    make_file "$src" 8388608
    rm -f "$dst" "$dst.gridflux.manifest" "$dst.part.$transfer_id"

    start_server "$dst" "$port" 1 65536 crc32c false "${tmp_dir}/temp-partial.server.log"
    set +e
    run_client "$src" "$port" 1 1048576 65536 crc32c \
        "${tmp_dir}/temp-partial.client.log" --transfer-id "$transfer_id" --max-chunks 2
    wait "$server_pid"
    set -e
    server_pid=""
    test -f "$dst.part.$transfer_id"
    flip_byte "$dst.part.$transfer_id" 0

    start_server "$dst" "$port" 1 65536 crc32c true "${tmp_dir}/temp-resume.server.log"
    run_client "$src" "$port" 1 1048576 65536 crc32c \
        "${tmp_dir}/temp-resume.client.log" --transfer-id "$transfer_id" --resume
    wait "$server_pid"
    server_pid=""
    cmp "$src" "$dst"
}

run_manifest_corruption_case() {
    local src="${tmp_dir}/manifest-corrupt.src"
    local dst="${tmp_dir}/manifest-corrupt.dst"
    local transfer_id="phase2b-manifest-corrupt"
    local port="$1"
    make_file "$src" 4194304
    rm -f "$dst" "$dst.gridflux.manifest" "$dst.part.$transfer_id"

    start_server "$dst" "$port" 1 65536 crc32c false "${tmp_dir}/manifest-partial.server.log"
    set +e
    run_client "$src" "$port" 1 1048576 65536 crc32c \
        "${tmp_dir}/manifest-partial.client.log" --transfer-id "$transfer_id" --max-chunks 1
    wait "$server_pid"
    set -e
    server_pid=""
    python3 - "$dst.gridflux.manifest" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()
path.write_text(text.replace("checksum_algorithm=crc32c", "checksum_algorithm=none", 1))
PY

    start_server "$dst" "$port" 1 65536 crc32c true "${tmp_dir}/manifest-resume.server.log"
    set +e
    run_client "$src" "$port" 1 1048576 65536 crc32c \
        "${tmp_dir}/manifest-resume.client.log" --transfer-id "$transfer_id" --resume
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""
    expect_failed_transfer "$client_status" "$server_status" "$dst" "manifest corruption resume"
}

run_client_corruption_case() {
    local name="$1"
    local flag="$2"
    local port="$3"
    local src="${tmp_dir}/${name}.src"
    local dst="${tmp_dir}/${name}.dst"
    make_file "$src" 4194304
    rm -f "$dst" "$dst.gridflux.manifest" "$dst".part.*

    start_server "$dst" "$port" 1 65536 crc32c false "${tmp_dir}/${name}.server.log"
    set +e
    run_client "$src" "$port" 1 1048576 65536 crc32c "${tmp_dir}/${name}.client.log" "$flag" 0
    local client_status=$?
    wait "$server_pid"
    local server_status=$?
    set -e
    server_pid=""
    expect_failed_transfer "$client_status" "$server_status" "$dst" "$name"
}

run_success_case "crc32c" 16777216 4 crc32c "$port_base"
run_success_case "none" 4194304 2 none "$((port_base + 1))"
run_partial_resume_case "$((port_base + 2))"
run_temp_corruption_case "$((port_base + 3))"
run_manifest_corruption_case "$((port_base + 4))"
run_client_corruption_case "corrupt-chunk" "--corrupt-chunk" "$((port_base + 5))"
run_client_corruption_case "duplicate-corrupt-chunk" "--duplicate-corrupt-chunk" "$((port_base + 6))"

echo "gridflux file checksum smoke passed"
