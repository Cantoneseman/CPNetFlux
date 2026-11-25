#!/bin/bash
# Read-only checks on both ends: tools, free ports, disk space and the route to SERVER_HOST.
source "$(dirname "$0")/lib.sh"
problems=0
need() { command -v "$1" >/dev/null || { echo "  missing on client: $1"; problems=$((problems + 1)); }; }

echo "client: $(hostname) $(id -un)"
for tool in globus-url-copy grid-proxy-init openssl python3 flock timeout sha256sum git; do need "$tool"; done
[ -n "$CPN_BIN_DIR" ] || for tool in cmake g++; do need "$tool"; done
command -v iperf3 >/dev/null || echo "  note: iperf3 missing on client; the link baseline is skipped"
echo "  route: $(ip route get "$SERVER_HOST" 2>/dev/null | head -1)"

big_bytes=$((BIG_MIB * 1024 * 1024 * 2 + SMALL_COUNT * SMALL_KIB * 1024))
echo "dataset: $((big_bytes / 1024 / 1024)) MiB; each end needs about twice that free"

mkdir -p "$KIT_HOME"
client_free=$(df -Pk "$KIT_HOME" | awk 'NR == 2 {print $4 * 1024}')
[ "$client_free" -gt $((big_bytes * 2)) ] || { echo "  not enough space under $KIT_HOME"; problems=$((problems + 1)); }

echo "server: $SERVER_SSH ($SERVER_HOST)"
server_report=$(on_server "
missing=''
for tool in globus-gridftp-server sha256sum python3 $( [ -n "$CPN_BIN_DIR" ] || echo cmake g++ ); do
  command -v \$tool >/dev/null || missing=\"\$missing \$tool\"
done
command -v iperf3 >/dev/null || echo 'note: iperf3 missing on server; the link baseline is skipped'
[ -z \"\$missing\" ] || echo \"missing on server:\$missing\"
mkdir -p $REMOTE_KIT_HOME
echo free=\$(df -Pk $REMOTE_KIT_HOME | awk 'NR == 2 {print \$4 * 1024}')
listening() { ss -Htln \"( sport >= :\$1 and sport <= :\$2 )\" | head -3; }
for range in $GFTP_PORT:$GFTP_PORT ${GFTP_DATA_PORTS/,/:} $CPN_PORT:$CPN_PORT $CPN_DATA_PORT_BASE:$((CPN_DATA_PORT_BASE + 511)) $IPERF_PORT:$IPERF_PORT; do
  used=\$(listening \${range%:*} \${range#*:})
  [ -z \"\$used\" ] || echo \"port range \$range already in use: \$used\"
done
ip -br addr | grep -F -w $SERVER_HOST >/dev/null || [ $SERVER_HOST = 127.0.0.1 ] || echo 'note: SERVER_HOST is not a local address on the server (NAT?); set GFTP_DATA_INTERFACE to the public IP'
") || die "cannot reach the server with: $SERVER_SSH"
echo "$server_report" | sed 's/^/  /'
server_free=$(echo "$server_report" | sed -n 's/^free=//p')
[ "${server_free:-0}" -gt $((big_bytes * 2)) ] || { echo "  not enough space under $REMOTE_KIT_HOME"; problems=$((problems + 1)); }
problems=$((problems + $(echo "$server_report" | grep -c -E '^(missing|port range)')))

if [ "$problems" -eq 0 ]; then echo "preflight ok"; else echo "preflight: $problems problem(s)"; exit 1; fi
