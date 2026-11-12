#!/bin/bash
set -euo pipefail
base=/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01
archive=/tmp/cpnetflux-frame-sendmsg-01-20261029/source-87b389f.tar.gz
printf '%s  %s\n' eac263310902ffd48d18bf1cb92c6598ebd5ef0724907275d00504fad47a6ff8 "$archive" | sha256sum -c -
install -d -m 700 "$base/source" "$base/build" "$base/evidence"
tar -xzf "$archive" -C "$base/source"
cmake -S "$base/source" -B "$base/build" -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_BUILD_DESKTOP=OFF -DCPNETFLUX_BUILD_TESTS=ON > "$base/evidence/cmake-configure.log" 2>&1
cmake --build "$base/build" -j2 > "$base/evidence/cmake-build.log" 2>&1
export CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 24)"
ctest --test-dir "$base/build" -R 'FramedDataSocketTest|cpnetflux_v3_interrupted_resume_smoke' --output-on-failure -j2 > "$base/evidence/ctest-targeted.log" 2>&1
ctest --test-dir "$base/build" --output-on-failure -j2 > "$base/evidence/ctest-full.log" 2>&1
unset CPNETFLUX_TEST_TOKEN
sha256sum "$base/build/cpnetflux-gridftp-server" "$base/build/cpnetflux-tree-upload-client" > "$base/evidence/binary-sha256.txt"
cat "$base/evidence/binary-sha256.txt"
tail -8 "$base/evidence/ctest-full.log"
