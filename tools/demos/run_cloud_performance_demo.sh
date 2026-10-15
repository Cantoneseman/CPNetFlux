#!/usr/bin/env bash
set -euo pipefail

# Short, live-recording matrix. It assumes the CPNetFlux server and GridFTP GSI
# service were prepared separately; credentials are supplied only by environment
# variables and never written into this script.
: "${CPNETFLUX_CLIENT_BIN_DIR:?set local CPNetFlux client binary directory}"
: "${CPNETFLUX_HOST:?set CPNetFlux server host}"
: "${CPNETFLUX_PORT:?set CPNetFlux server control port}"
: "${CPNETFLUX_SERVER_NAME:?set CPNetFlux TLS server name}"
: "${CPNETFLUX_CA_FILE:?set CPNetFlux CA file}"
: "${CPNETFLUX_CERT_FILE:?set CPNetFlux client certificate file}"
: "${CPNETFLUX_KEY_FILE:?set CPNetFlux client key file}"
: "${CPNETFLUX_TOKEN_FILE:?set CPNetFlux token file}"
: "${GRIDFTP_URL:?set GridFTP host:port, for example node.grid:2811}"
: "${X509_USER_PROXY:?set GSI proxy path}"

WORK_DIR="${WORK_DIR:-/tmp/cpnetflux-cloud-performance-demo}"
REPETITIONS="${REPETITIONS:-1}"
mkdir -p "$WORK_DIR"

dataset() {
  local root="$1" profile="$2"
  rm -rf "$root"
  mkdir -p "$root"
  python3 - "$root" "$profile" <<'PY'
import hashlib, pathlib, sys
root=pathlib.Path(sys.argv[1]); profile=sys.argv[2]
if profile == "single_256MiB":
    spec=[("single.bin",256*1024*1024)]
elif profile == "tree_dense_128MiB":
    spec=[(f"tree-dense/file-{i:04d}.bin",1024*1024) for i in range(128)]
elif profile == "tree_mixed_256MiB":
    spec=[*( (f"tree-mixed/file-{i:04d}.bin",512*1024) for i in range(128)),
          *( (f"tree-mixed/file-{128+i:04d}.bin",4*1024*1024) for i in range(16)),
          *( (f"tree-mixed/file-{144+i:04d}.bin",32*1024*1024) for i in range(4))]
else: raise SystemExit(f"unknown profile {profile}")
for rel,size in spec:
    path=root/rel; path.parent.mkdir(parents=True,exist_ok=True)
    left=size; n=0
    with path.open("wb") as out:
        while left:
            block=hashlib.sha256(f"cloud-demo:{profile}:{rel}:{n}".encode()).digest()*128
            block=block[:min(left,len(block))]; out.write(block); left-=len(block); n+=1
PY
}

bytes_in() {
  python3 - "$1" <<'PY'
from pathlib import Path
import sys
print(sum(p.stat().st_size for p in Path(sys.argv[1]).rglob("*") if p.is_file()))
PY
}

run_cp() {
  local direction="$1" profile="$2" source="$3" destination="$4" case="$5"
  local summary="$WORK_DIR/${case}.json" start end bytes
  if [[ "$direction" == upload ]]; then
    bytes=$(bytes_in "$source")
  else
    bytes=0
  fi
  start=$(date +%s%N)
  if [[ "$direction" == upload ]]; then
    "$CPNETFLUX_CLIENT_BIN_DIR/cpnetflux-tree-upload-client" \
      --host "$CPNETFLUX_HOST" --port "$CPNETFLUX_PORT" \
      --source-dir "$source" --dest-dir "$destination" \
      --connections 4 --file-parallelism 4 --chunk-size 1048576 --buffer-size 1048576 \
      --checksum crc32c --checksum-backend auto --control-reuse worker \
      --data-session-reuse tree --file-scheduling dynamic --data-pending-window 2 \
      --compression auto --control-pipeline-depth 2 --scheduler off \
      --auth-mode token --auth-token-file "$CPNETFLUX_TOKEN_FILE" \
      --phase-timing on --tls-mode required --tls-server-name "$CPNETFLUX_SERVER_NAME" \
      --tls-ca-file "$CPNETFLUX_CA_FILE" --tls-cert-file "$CPNETFLUX_CERT_FILE" \
      --tls-key-file "$CPNETFLUX_KEY_FILE" --data-tls-mode required \
      --json-summary "$summary"
  else
    "$CPNETFLUX_CLIENT_BIN_DIR/cpnetflux-tree-download-client" \
      --host "$CPNETFLUX_HOST" --port "$CPNETFLUX_PORT" \
      --source-dir "$destination" --dest-dir "$source" \
      --connections 4 --file-parallelism 4 --chunk-size 1048576 --buffer-size 1048576 \
      --checksum crc32c --checksum-backend auto --control-reuse worker \
      --data-session-reuse tree --file-scheduling dynamic --data-pending-window 2 \
      --compression auto --control-pipeline-depth 2 --scheduler off \
      --auth-mode token --auth-token-file "$CPNETFLUX_TOKEN_FILE" \
      --phase-timing on --tls-mode required --tls-server-name "$CPNETFLUX_SERVER_NAME" \
      --tls-ca-file "$CPNETFLUX_CA_FILE" --tls-cert-file "$CPNETFLUX_CERT_FILE" \
      --tls-key-file "$CPNETFLUX_KEY_FILE" --data-tls-mode required \
      --json-summary "$summary"
  fi
  end=$(date +%s%N)
  python3 - "$summary" "$profile" "$direction" "$start" "$end" "$bytes" <<'PY'
import json,sys
summary=json.load(open(sys.argv[1])); elapsed=(int(sys.argv[4])-int(sys.argv[3]))/1e9
bytes_value=summary.get("bytes_total") or int(sys.argv[5])
print(f"PERF system=cpnetflux-v3-01c profile={sys.argv[2]} direction={sys.argv[3]} result={summary.get('result')} throughput_gbps={summary.get('throughput_gbps')} elapsed_seconds={elapsed:.6f} bytes={bytes_value} actual_mode={summary.get('actual_mode')} pipeline={summary.get('control_pipeline_actual_mode')}")
PY
}

run_gridftp() {
  local direction="$1" profile="$2" source="$3" remote="$4" case="$5"
  local destination="$WORK_DIR/download-${case}" start end bytes target
  bytes=$(bytes_in "$source")
  target="gsiftp://${GRIDFTP_URL}/${remote}/"
  start=$(date +%s%N)
  if [[ "$direction" == upload ]]; then
    globus-url-copy -fast -dcpriv -cd -rp -r -p 8 -cc 8 -tcp-bs 1048576 \
      "file://${source}/" "$target"
  else
    rm -rf "$destination"; mkdir -p "$destination"
    globus-url-copy -fast -dcpriv -cd -rp -r -p 8 -cc 8 -tcp-bs 1048576 \
      "$target" "file://${destination}/"
  fi
  end=$(date +%s%N)
  python3 - "$profile" "$direction" "$start" "$end" "$bytes" <<'PY'
import sys
elapsed=(int(sys.argv[4])-int(sys.argv[3]))/1e9
gbps=int(sys.argv[5])*8/elapsed/1e9
print(f"PERF system=gridftp-gsi profile={sys.argv[1]} direction={sys.argv[2]} result=pass throughput_gbps={gbps:.6f} elapsed_seconds={elapsed:.6f} bytes={sys.argv[5]}")
PY
}

for profile in single_256MiB tree_dense_128MiB tree_mixed_256MiB; do
  for rep in $(seq 1 "$REPETITIONS"); do
    source="$WORK_DIR/source-${profile}-r${rep}"
    dataset "$source" "$profile"
    cp_case="${profile}-r${rep}-cp"
    grid_case="${profile}-r${rep}-grid"
    run_cp upload "$profile" "$source" "demo/${cp_case}" "$cp_case-upload"
    run_gridftp upload "$profile" "$source" "demo/${grid_case}" "$grid_case-upload"
    rm -rf "$WORK_DIR/download-${cp_case}-download" "$WORK_DIR/download-${grid_case}-download"
    run_cp download "$profile" "$WORK_DIR/download-${cp_case}-download" "demo/${cp_case}" "$cp_case-download"
    run_gridftp download "$profile" "$source" "demo/${grid_case}" "$grid_case-download"
    rm -rf "$source" "$WORK_DIR/download-${cp_case}-download" "$WORK_DIR/download-${grid_case}-download"
  done
done

echo "PERF result=pass work_dir=$WORK_DIR repetitions=$REPETITIONS"
