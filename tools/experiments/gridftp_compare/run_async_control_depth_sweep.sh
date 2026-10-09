#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes)
REMOTE="${CPNETFLUX_EXPERIMENT_REMOTE:?Set SSH destination for the Shanghai peer}"
CONTROL_HOST="${CPNETFLUX_EXPERIMENT_CONTROL_HOST:?Set the Shanghai reachable address}"
LOCAL_BUILD_DIR="${CPNETFLUX_EXPERIMENT_BUILD_DIR:?Set the isolated Shenzhen build directory}"
EXPECTED_COMMIT="${CPNETFLUX_EXPERIMENT_EXPECTED_COMMIT:-}"
OUTPUT_ROOT="${CPNETFLUX_EXPERIMENT_OUTPUT_ROOT:-/tmp/cpnetflux-runs/dir-async-control-evidence}"
RUN_ID="${CPNETFLUX_EXPERIMENT_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
OUT="${OUTPUT_ROOT%/}/${RUN_ID}"
REMOTE_WORK_ROOT="/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}/runs"
REMOTE_BUILD_DIR="/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}/peer-bin"
GRIDFTP_AUTH_MODE="${CPNETFLUX_EXPERIMENT_GRIDFTP_AUTH_MODE:-anonymous}"
GRIDFTP_HOME_DIR="/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}/gridftp-home"
GRIDFTP_CONTROL_PORT="${CPNETFLUX_EXPERIMENT_GRIDFTP_CONTROL_PORT:-22410}"
GRIDFTP_DATA_PORT_BASE="${CPNETFLUX_EXPERIMENT_GRIDFTP_DATA_PORT_BASE:-35000}"
CPNETFLUX_CONTROL_PORT_BASE="${CPNETFLUX_EXPERIMENT_CPNETFLUX_CONTROL_PORT_BASE:-21210}"
CPNETFLUX_DATA_PORT_BASE="${CPNETFLUX_EXPERIMENT_CPNETFLUX_DATA_PORT_BASE:-34000}"
SEED="${CPNETFLUX_EXPERIMENT_SEED:-20260831}"
REPEATS="${CPNETFLUX_EXPERIMENT_REPEATS:-3}"
MIN_FREE_GIB="${CPNETFLUX_EXPERIMENT_MIN_FREE_GIB:-10}"
BUILD_JOBS="${CPNETFLUX_EXPERIMENT_BUILD_JOBS:-2}"
RUNNER="$ROOT/tools/experiments/gridftp_compare/run_gridftp_compare_experiment.py"
COMPARATOR="$ROOT/tools/experiments/gridftp_compare/compare_async_control_runs.py"

fail() {
  echo "ERROR: $*" >&2
  exit 2
}

[[ "$RUN_ID" =~ ^[A-Za-z0-9._-]+$ ]] || fail "RUN_ID may contain only letters, digits, dot, underscore, and hyphen"
[[ "$GRIDFTP_AUTH_MODE" == anonymous ]] || fail "short matched test uses anonymous/plain data for both tools; GSI privacy is a different configuration"
[[ "$REPEATS" == 3 ]] || fail "this short matrix requires exactly three repeats"
[[ "$MIN_FREE_GIB" =~ ^[0-9]+$ ]] || fail "MIN_FREE_GIB must be an integer GiB value"
mkdir -p "$OUTPUT_ROOT"
exec 9>"$OUTPUT_ROOT/.depth-sweep.lock"
flock -n 9 || fail "another depth sweep is active"
[[ -e "$OUT" ]] && fail "refusing to reuse output directory: $OUT"
[[ -x "$RUNNER" || -f "$RUNNER" ]] || fail "missing runner: $RUNNER"
[[ -f "$COMPARATOR" ]] || fail "missing comparator: $COMPARATOR"
[[ -z "$(git -C "$ROOT" status --porcelain)" ]] || {
  echo "Refusing to run from a dirty worktree." >&2
  git -C "$ROOT" status --short >&2
  exit 2
}
REPO_ROOT="$(git -C "$ROOT" rev-parse --show-toplevel)"
[[ "$REPO_ROOT" == "$ROOT" ]] || fail "repository root mismatch: $ROOT"
COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
[[ -z "$EXPECTED_COMMIT" || "$COMMIT" == "$EXPECTED_COMMIT" ]] || fail "HEAD $COMMIT does not match expected commit $EXPECTED_COMMIT"

REQUIRED_LOCAL_GIB=$((MIN_FREE_GIB + 3))
REQUIRED_REMOTE_GIB=$((MIN_FREE_GIB + 1))
mkdir -p "$LOCAL_BUILD_DIR"
OUTPUT_FREE_GIB="$(df -Pk "$OUTPUT_ROOT" | awk 'NR==2 {print int($4/1024/1024)}')"
BUILD_FREE_GIB="$(df -Pk "$LOCAL_BUILD_DIR" | awk 'NR==2 {print int($4/1024/1024)}')"
(( OUTPUT_FREE_GIB >= REQUIRED_LOCAL_GIB )) || fail "output mount has ${OUTPUT_FREE_GIB} GiB free; need ${REQUIRED_LOCAL_GIB} GiB"
(( BUILD_FREE_GIB >= REQUIRED_LOCAL_GIB )) || fail "build mount has ${BUILD_FREE_GIB} GiB free; need ${REQUIRED_LOCAL_GIB} GiB"

SOURCE_ARCHIVE_SHA="$(git -C "$ROOT" archive --format=tar "$COMMIT" | sha256sum | awk '{print $1}')"
cmake -S "$ROOT" -B "$LOCAL_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF
CMAKE_SOURCE_ROOT="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$LOCAL_BUILD_DIR/CMakeCache.txt")"
[[ "$CMAKE_SOURCE_ROOT" == "$ROOT" ]] || fail "CMake source root mismatch: $CMAKE_SOURCE_ROOT"
cmake --build "$LOCAL_BUILD_DIR" --parallel "$BUILD_JOBS" --target cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server
for binary in cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server; do
  [[ -x "$LOCAL_BUILD_DIR/$binary" ]] || fail "missing executable: $LOCAL_BUILD_DIR/$binary"
done
REQUIRED_LOCAL_GIB=$((MIN_FREE_GIB + 3))
REQUIRED_REMOTE_GIB=$((MIN_FREE_GIB + 1))
OUTPUT_FREE_GIB="$(df -Pk "$OUTPUT_ROOT" | awk 'NR==2 {print int($4/1024/1024)}')"
BUILD_FREE_GIB="$(df -Pk "$LOCAL_BUILD_DIR" | awk 'NR==2 {print int($4/1024/1024)}')"
(( OUTPUT_FREE_GIB >= REQUIRED_LOCAL_GIB )) || fail "output mount has ${OUTPUT_FREE_GIB} GiB free; need ${REQUIRED_LOCAL_GIB} GiB"
(( BUILD_FREE_GIB >= REQUIRED_LOCAL_GIB )) || fail "build mount has ${BUILD_FREE_GIB} GiB free; need ${REQUIRED_LOCAL_GIB} GiB"
REMOTE_FREE_GIB="$(ssh "${SSH_OPTS[@]}" "$REMOTE" "df -Pk /tmp | awk 'NR==2 {print int(\$4/1024/1024)}'")"
[[ "$REMOTE_FREE_GIB" =~ ^[0-9]+$ ]] || fail "remote /tmp free-space probe returned: $REMOTE_FREE_GIB"
(( REMOTE_FREE_GIB >= REQUIRED_REMOTE_GIB )) || fail "Shanghai /tmp has ${REMOTE_FREE_GIB} GiB free; need ${REQUIRED_REMOTE_GIB} GiB"

remote_port_check() {
  ssh "${SSH_OPTS[@]}" "$REMOTE" python3 - "$@" <<'PY'
import socket
import sys
ports = [int(value) for value in sys.argv[1:]]
occupied = []
for port in ports:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        if sock.connect_ex(("127.0.0.1", port)) == 0:
            occupied.append(port)
if occupied:
    print("occupied=" + ",".join(str(port) for port in occupied))
    raise SystemExit(2)
print("free=" + ",".join(str(port) for port in ports))
PY
}
REMOTE_PORTS=()
for offset in $(seq 0 11); do
  REMOTE_PORTS+=("$((CPNETFLUX_CONTROL_PORT_BASE + offset))")
done
# The server may allocate any port in its 512-port passive window.
for offset in $(seq 0 522); do
  REMOTE_PORTS+=("$((CPNETFLUX_DATA_PORT_BASE + offset))")
done
REMOTE_PORTS+=("$GRIDFTP_CONTROL_PORT")
for offset in $(seq 0 511); do
  REMOTE_PORTS+=("$((GRIDFTP_DATA_PORT_BASE + offset))")
done
remote_port_check "${REMOTE_PORTS[@]}" || fail "CPNetFlux control/data port preflight failed"
if [[ "$GRIDFTP_AUTH_MODE" == "gsi" ]]; then
  ssh "${SSH_OPTS[@]}" "$REMOTE" "ss -ltn 'sport = :$GRIDFTP_CONTROL_PORT' | grep -q LISTEN" || fail "expected existing GridFTP control port $GRIDFTP_CONTROL_PORT is not listening"
fi

BUILD_MANIFEST="$LOCAL_BUILD_DIR/cpnetflux-build-manifest.txt"
UPLOAD_SHA="$(sha256sum "$LOCAL_BUILD_DIR/cpnetflux-tree-upload-client" | awk '{print $1}')"
DOWNLOAD_SHA="$(sha256sum "$LOCAL_BUILD_DIR/cpnetflux-tree-download-client" | awk '{print $1}')"
SERVER_SHA="$(sha256sum "$LOCAL_BUILD_DIR/cpnetflux-gridftp-server" | awk '{print $1}')"
printf '%s\n' "source_root=$ROOT" "commit=$COMMIT" "source_archive_sha256=$SOURCE_ARCHIVE_SHA" "upload_sha256=$UPLOAD_SHA" "download_sha256=$DOWNLOAD_SHA" "server_sha256=$SERVER_SHA" > "$BUILD_MANIFEST"
if ssh "${SSH_OPTS[@]}" "$REMOTE" "test ! -e '$REMOTE_BUILD_DIR'"; then
  :
else
  fail "refusing to reuse remote build directory: $REMOTE_BUILD_DIR"
fi
ssh "${SSH_OPTS[@]}" "$REMOTE" "mkdir -p '/tmp/cpnetflux-runs' && mkdir '/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}' && mkdir '$REMOTE_BUILD_DIR'"
scp "${SSH_OPTS[@]}" "$LOCAL_BUILD_DIR/cpnetflux-gridftp-server" "$REMOTE:$REMOTE_BUILD_DIR/cpnetflux-gridftp-server"
ssh "${SSH_OPTS[@]}" "$REMOTE" "chmod 700 '$REMOTE_BUILD_DIR/cpnetflux-gridftp-server'"
REMOTE_SERVER_SHA="$(ssh "${SSH_OPTS[@]}" "$REMOTE" "sha256sum '$REMOTE_BUILD_DIR/cpnetflux-gridftp-server' | awk '{print \$1}'")"
[[ "$SERVER_SHA" == "$REMOTE_SERVER_SHA" ]] || fail "peer binary SHA-256 mismatch"

mkdir "$OUT"
cp "$BUILD_MANIFEST" "$OUT/build-manifest.txt"
cp "$LOCAL_BUILD_DIR/CMakeCache.txt" "$OUT/CMakeCache.txt"
cat > "$OUT/experiment-input.txt" <<EOF
commit=$COMMIT
source_archive_sha256=$SOURCE_ARCHIVE_SHA
branch=$(git -C "$ROOT" branch --show-current)
remote=$REMOTE
control_host=$CONTROL_HOST
local_build_dir=$LOCAL_BUILD_DIR
remote_work_root=$REMOTE_WORK_ROOT
remote_build_dir=$REMOTE_BUILD_DIR
gridftp_auth_mode=$GRIDFTP_AUTH_MODE
gridftp_home_dir=$GRIDFTP_HOME_DIR
data_protection=none_both_systems
build_manifest=$BUILD_MANIFEST
required_local_free_gib=$REQUIRED_LOCAL_GIB
required_remote_free_gib=$REQUIRED_REMOTE_GIB
output_mount_free_gib=$OUTPUT_FREE_GIB
build_mount_free_gib=$BUILD_FREE_GIB
gridftp_control_port=$GRIDFTP_CONTROL_PORT
gridftp_data_port_base=$GRIDFTP_DATA_PORT_BASE
cpnetflux_control_port_base=$CPNETFLUX_CONTROL_PORT_BASE
cpnetflux_data_port_base=$CPNETFLUX_DATA_PORT_BASE
remote_tmp_free_gib=$REMOTE_FREE_GIB
seed=$SEED
repeats=$REPEATS
preset=async-control-short
dataset=tree_dense_128MiB
file_parallelism=1
per_file_connections=1
directions=local_to_remote,remote_to_local
depths=0,1,2,4
cpnetflux_tree_upload_sha256=$UPLOAD_SHA
cpnetflux_tree_download_sha256=$DOWNLOAD_SHA
cpnetflux_gridftp_server_sha256=$SERVER_SHA
peer_cpnetflux_gridftp_server_sha256=$REMOTE_SERVER_SHA
EOF

COMMON=(
  --stage async-control
  --case-preset async-control-short
  --dataset-profile tree_dense_128MiB
  --directions local_to_remote,remote_to_local
  --repeat "$REPEATS"
  --seed "$SEED"
  --control-reuse worker
  --remote "$REMOTE"
  --control-host "$CONTROL_HOST"
  --control-port "$GRIDFTP_CONTROL_PORT"
  --data-port-base "$GRIDFTP_DATA_PORT_BASE"
  --cpnetflux-control-port-base "$CPNETFLUX_CONTROL_PORT_BASE"
  --cpnetflux-data-port-base "$CPNETFLUX_DATA_PORT_BASE"
  --remote-work-root "$REMOTE_WORK_ROOT"
  --local-build-dir "$LOCAL_BUILD_DIR"
  --remote-build-dir "$REMOTE_BUILD_DIR"
  --gridftp-auth-mode "$GRIDFTP_AUTH_MODE"
  --gridftp-home-dir "$GRIDFTP_HOME_DIR"
  --min-free-gib "$MIN_FREE_GIB"
  --phase-timing on
  --case-timeout 180
)

run_depth() {
  local depth="$1"
  local systems="$2"
  local output="$3"
  python3 "$RUNNER" "${COMMON[@]}" \
    --systems "$systems" \
    --control-pipeline-depth "$depth" \
    --output-dir "$output"
}

BASELINE="$OUT/depth0-gridftp"
run_depth 0 cpnetflux,gridftp "$BASELINE"
for depth in 1 2 4; do
  run_depth "$depth" cpnetflux "$OUT/depth${depth}"
done

python3 "$COMPARATOR" \
  --baseline-run "$BASELINE" \
  --depth-run "1=$OUT/depth1" \
  --depth-run "2=$OUT/depth2" \
  --depth-run "4=$OUT/depth4" \
  --expected-repeats "$REPEATS" \
  --expected-directions local_to_remote,remote_to_local \
  --dataset tree_dense_128MiB \
  --file-parallelism 1 \
  --per-file-connections 1 \
  --output "$OUT/comparison.md"

cat > "$OUT/complete.txt" <<EOF
comparison_status=data_complete_only
performance_acceptance=see_comparison_report
baseline=$BASELINE
depth1=$OUT/depth1
depth2=$OUT/depth2
depth4=$OUT/depth4
comparison=$OUT/comparison.md
EOF
echo "Async-control depth sweep complete: $OUT/comparison.md"
