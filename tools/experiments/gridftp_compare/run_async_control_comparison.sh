#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REMOTE="${CPNETFLUX_EXPERIMENT_REMOTE:?Set SSH destination for the Shanghai peer, e.g. root@<host>}"
CONTROL_HOST="${CPNETFLUX_EXPERIMENT_CONTROL_HOST:?Set the Shanghai GridFTP/CPNetFlux reachable address}"
LOCAL_BUILD_DIR="${CPNETFLUX_EXPERIMENT_BUILD_DIR:?Set the isolated Linux build directory for this commit}"
GRIDFTP_HOME_DIR="${CPNETFLUX_EXPERIMENT_GRIDFTP_HOME:?Set the authorized GridFTP test home directory}"
RUN_ID="${CPNETFLUX_EXPERIMENT_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
OUTPUT_ROOT="${CPNETFLUX_EXPERIMENT_OUTPUT_ROOT:-/tmp/cpnetflux-runs/dir-async-control-evidence}"
REMOTE_WORK_ROOT="/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}/runs"
REMOTE_BUILD_DIR="/tmp/cpnetflux-runs/dir-async-control-${RUN_ID}/peer-bin"
GRIDFTP_AUTH_MODE="${CPNETFLUX_EXPERIMENT_GRIDFTP_AUTH_MODE:-gsi}"
GRIDFTP_CONTROL_PORT="${CPNETFLUX_EXPERIMENT_GRIDFTP_CONTROL_PORT:-2811}"
GRIDFTP_DATA_PORT_BASE="${CPNETFLUX_EXPERIMENT_GRIDFTP_DATA_PORT_BASE:-32000}"
CPNETFLUX_CONTROL_PORT_BASE="${CPNETFLUX_EXPERIMENT_CPNETFLUX_CONTROL_PORT_BASE:-21210}"
CPNETFLUX_DATA_PORT_BASE="${CPNETFLUX_EXPERIMENT_CPNETFLUX_DATA_PORT_BASE:-34000}"
BUILD_JOBS="${CPNETFLUX_EXPERIMENT_BUILD_JOBS:-2}"
OUT="${OUTPUT_ROOT%/}/${RUN_ID}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes)
if [[ ! "$RUN_ID" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "RUN_ID may contain only letters, digits, dot, underscore, and hyphen." >&2
  exit 2
fi

if [[ -e "$OUT" ]]; then
  echo "Refusing to reuse existing output directory: $OUT" >&2
  echo "Set a new CPNETFLUX_EXPERIMENT_RUN_ID or resume the runner outputs manually." >&2
  exit 2
fi
if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
  echo "Refusing to benchmark a dirty worktree. Commit/push the exact reviewed task first." >&2
  git -C "$ROOT" status --short >&2
  exit 2
fi
if [[ "$(git -C "$ROOT" rev-parse --show-toplevel)" != "$ROOT" ]]; then
  echo "Repository root mismatch: $ROOT" >&2
  exit 2
fi
if [[ ! -f "$LOCAL_BUILD_DIR/CMakeCache.txt" ]]; then
  echo "Build directory is not configured: $LOCAL_BUILD_DIR" >&2
  exit 2
fi
FREE_GIB="$(df -Pk /tmp | awk 'NR==2 {print int($4/1024/1024)}')"
if (( FREE_GIB < 13 )); then
  echo "Shenzhen /tmp has only ${FREE_GIB} GiB free; need 13 GiB before build/experiment." >&2
  exit 2
fi
cmake --build "$LOCAL_BUILD_DIR" --parallel "$BUILD_JOBS"
for binary in cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux-gridftp-server; do
  test -x "$LOCAL_BUILD_DIR/$binary" || { echo "Missing executable: $LOCAL_BUILD_DIR/$binary" >&2; exit 2; }
done
ssh "${SSH_OPTS[@]}" "$REMOTE" "python3 -c 'import shutil,sys; f=shutil.disk_usage(\"/tmp\").free; print(f\"Shanghai /tmp free: {f/1024**3:.2f} GiB\"); sys.exit(0 if f >= 11*1024**3 else 2)'"
ssh "${SSH_OPTS[@]}" "$REMOTE" "test ! -e '$REMOTE_BUILD_DIR' && mkdir -p '$REMOTE_BUILD_DIR'"
scp "${SSH_OPTS[@]}" "$LOCAL_BUILD_DIR/cpnetflux-gridftp-server" "$REMOTE:$REMOTE_BUILD_DIR/cpnetflux-gridftp-server"
ssh "${SSH_OPTS[@]}" "$REMOTE" "chmod 700 '$REMOTE_BUILD_DIR/cpnetflux-gridftp-server'"
LOCAL_SERVER_SHA="$(sha256sum "$LOCAL_BUILD_DIR/cpnetflux-gridftp-server" | awk '{print $1}')"
REMOTE_SERVER_SHA="$(ssh "${SSH_OPTS[@]}" "$REMOTE" "sha256sum '$REMOTE_BUILD_DIR/cpnetflux-gridftp-server' | cut -d ' ' -f1")"
if [[ "$LOCAL_SERVER_SHA" != "$REMOTE_SERVER_SHA" ]]; then
  echo "Peer binary SHA-256 mismatch; refusing to run." >&2
  exit 2
fi

mkdir -p "$OUT"
cat > "$OUT/experiment-input.txt" <<EOF
commit=$(git -C "$ROOT" rev-parse HEAD)
branch=$(git -C "$ROOT" branch --show-current)
remote=$REMOTE
control_host=$CONTROL_HOST
gridftp_auth_mode=$GRIDFTP_AUTH_MODE
gridftp_home_dir=$GRIDFTP_HOME_DIR
local_build_dir=$LOCAL_BUILD_DIR
remote_build_dir=$REMOTE_BUILD_DIR
cpnetflux_server_sha256=$LOCAL_SERVER_SHA
peer_server_sha256=$REMOTE_SERVER_SHA
tree_upload_client_sha256=$LOCAL_UPLOAD_SHA
tree_download_client_sha256=$LOCAL_DOWNLOAD_SHA
seed=20260831
repeats=3
directions=local_to_remote,remote_to_local
matrix=tree_dense_128MiB(fp1,4,8)x1;tree_mixed_256MiB(fp1/n1,fp2/n2,fp4/n2)
EOF

COMMON=(
  --stage async-control
  --directions local_to_remote,remote_to_local
  --repeat 3
  --seed 20260831
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
  --min-free-gib 10
)

python3 "$ROOT/tools/experiments/gridftp_compare/run_gridftp_compare_experiment.py" \
  "${COMMON[@]}" --systems cpnetflux,gridftp --control-pipeline-depth 0 \
  --output-dir "$OUT/depth0-and-gridftp"
python3 "$ROOT/tools/experiments/gridftp_compare/run_gridftp_compare_experiment.py" \
  "${COMMON[@]}" --systems cpnetflux --control-pipeline-depth 1 \
  --output-dir "$OUT/depth1"
python3 "$ROOT/tools/experiments/gridftp_compare/compare_async_control_runs.py" \
  --depth0-results "$OUT/depth0-and-gridftp/results.csv" \
  --depth1-results "$OUT/depth1/results.csv" \
  --output "$OUT/comparison.md"

echo "Experiment and comparison complete: $OUT/comparison.md"
echo "Remote binary retained for evidence: $REMOTE_BUILD_DIR/cpnetflux-gridftp-server"
