#!/bin/bash
# cleanup.sh [--all]: stops the kit servers and removes test data on both ends.
# Results, certificates and builds are kept unless --all is given.
source "$(dirname "$0")/lib.sh"
"$KIT_DIR/servers.sh" stop
for path in "$SERVER/gftp-root" "$SERVER/cpn-root"; do assert_kit_path "$path"; done
on_server "rm -rf $SERVER/gftp-root $SERVER/cpn-root"
for path in "$CLIENT/data" "$CLIENT/back"; do assert_kit_path "$path"; rm -rf "$path"; done
if [ "${1:-}" = --all ]; then
    assert_kit_path "$SERVER"; assert_kit_path "$CLIENT"
    on_server "rm -rf $SERVER"
    rm -rf "$CLIENT"
fi
log "cleaned"
