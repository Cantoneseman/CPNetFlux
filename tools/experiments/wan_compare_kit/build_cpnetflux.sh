#!/bin/bash
# Builds CPN_COMMIT on both ends from git archive into <kit>/cpn/<sha>; the checkout is untouched.
# Skipped when CPN_BIN_DIR points at an existing build.
source "$(dirname "$0")/lib.sh"
set -e
targets="cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client"
if [ -n "$CPN_BIN_DIR" ]; then
    for target in $targets; do [ -x "$CPN_BIN_DIR/$target" ] || die "$CPN_BIN_DIR/$target missing"; done
    log "using existing binaries in $CPN_BIN_DIR"
    exit 0
fi
sha=$(git -C "$CPN_REPO" rev-parse --verify "$CPN_COMMIT^{commit}")
log "commit $sha"
build_script() {  # root -> commands that configure and build in root
    cat <<EOF
set -e
cd $1
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=OFF $CPN_CMAKE_ARGS > configure.log 2>&1 \
    || { tail -30 configure.log; exit 1; }
$(nice_prefix) cmake --build build -j $BUILD_JOBS --target $targets > build.log 2>&1 \
    || { grep -E 'error' build.log | head -30; exit 1; }
# The assembler embeds the absolute build path; stripped binaries compare equal across ends.
cd build && strip $targets && sha256sum $targets
EOF
}

client_root=$CLIENT/cpn/$sha
rm -rf "$client_root"; mkdir -p "$client_root/src"
git -C "$CPN_REPO" archive "$sha" | tar -x -C "$client_root/src"
log "building on the client"
bash -c "$(build_script "$client_root")" > "$CLIENT/build-client.sha256"
echo "$client_root/build" > "$CLIENT/cpn-bin-dir"

server_root=$SERVER/cpn/$sha
if [ "$SERVER_SSH" = local ] && [ "$server_root" = "$client_root" ]; then
    cp "$CLIENT/build-client.sha256" "$CLIENT/build-server.sha256"
else
    on_server "rm -rf $server_root; mkdir -p $server_root/src"
    if [ "$SERVER_SSH" = local ]; then
        git -C "$CPN_REPO" archive "$sha" | tar -x -C "$server_root/src"
    else
        # shellcheck disable=SC2086
        git -C "$CPN_REPO" archive "$sha" | ssh $SSH_OPTS -o BatchMode=yes "$SERVER_SSH" "tar -x -C $server_root/src"
    fi
    log "building on the server"
    on_server "$(build_script "$server_root")" > "$CLIENT/build-server.sha256"
fi
on_server "echo $server_root/build > $SERVER/cpn-bin-dir"
echo "$sha" > "$CLIENT/cpn-commit"
log "client binaries:"; sed 's/^/  /' "$CLIENT/build-client.sha256"
log "server binaries:"; sed 's/^/  /' "$CLIENT/build-server.sha256"
cmp -s "$CLIENT/build-client.sha256" "$CLIENT/build-server.sha256" \
    || log "note: binary hashes differ between the ends (different compilers or libraries); both are commit $sha"
