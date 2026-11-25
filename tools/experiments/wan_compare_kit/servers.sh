#!/bin/bash
# servers.sh start|stop|status: the kit's GridFTP and CPNetFlux servers on the server host.
# Processes are tracked by pid files under $SERVER/run; nothing else is signalled.
source "$(dirname "$0")/lib.sh"
action=${1:-status}
bin=$(server_bin)
[ "$action" != start ] || [ -n "$bin" ] || die "no CPNetFlux build on the server; run build_cpnetflux.sh"

on_server "
set -u
cd $SERVER 2>/dev/null || { echo 'kit not set up on the server; run setup_certs.sh'; exit 1; }
mkdir -p run logs gftp-root cpn-root
alive() {  # name pattern
  local pid; pid=\$(cat run/\$1.pid 2>/dev/null) || return 1
  ps -p \"\$pid\" -o args= 2>/dev/null | grep -q -- \"\$2\"
}
stop() {  # name pattern
  if alive \$1 \"\$2\"; then kill \$(cat run/\$1.pid); sleep 1; fi
  rm -f run/\$1.pid
}
case $action in
start)
  allow_root=''
  if [ \"\$(id -u)\" = 0 ]; then
    allow_root=-allow-root
    echo 'note: running as root; a normal user account is recommended'
  fi
  alive gridftp globus-gridftp-server || {
    env GLOBUS_TCP_PORT_RANGE=$GFTP_DATA_PORTS X509_USER_CERT=$SERVER/pki/hostcert.pem \
      X509_USER_KEY=$SERVER/pki/hostkey.pem X509_CERT_DIR=$SERVER/pki/certificates \
      GRIDMAP=$SERVER/pki/grid-mapfile $(nice_prefix) globus-gridftp-server -S -p $GFTP_PORT \
      -data-interface $GFTP_DATA_INTERFACE -restrict-paths $SERVER/gftp-root \$allow_root \
      -pidfile $SERVER/run/gridftp.pid -log-level error,warn,info -l $SERVER/logs/gridftp.log
  }
  alive cpnetflux cpnetflux-gridftp-server || {
    nohup $(nice_prefix) $bin/cpnetflux-gridftp-server --root $SERVER/cpn-root --host $SERVER_HOST \
      --port $CPN_PORT --data-port-base $CPN_DATA_PORT_BASE --connections 16 \
      --auth-mode token --auth-token-file $SERVER/pki/token --tls-mode required \
      --tls-cert-file $SERVER/pki/cpn-cert.pem --tls-key-file $SERVER/pki/cpn-key.pem \
      --data-transport tcp > logs/cpnetflux.log 2>&1 < /dev/null &
    echo \$! > run/cpnetflux.pid
  }
  sleep 2 ;;
stop)
  stop gridftp globus-gridftp-server
  stop cpnetflux cpnetflux-gridftp-server
  stop iperf iperf3 ;;
esac
for entry in gridftp:globus-gridftp-server:$GFTP_PORT cpnetflux:cpnetflux-gridftp-server:$CPN_PORT; do
  IFS=: read -r name pattern port <<< \"\$entry\"
  if alive \$name \$pattern; then
    state=\"running pid \$(cat run/\$name.pid)\"
    ss -Htln \"( sport = :\$port )\" | grep -q . && state=\"\$state, listening on \$port\" || state=\"\$state, NOT listening on \$port\"
  else
    state=stopped
  fi
  echo \"\$name: \$state\"
done
[ $action != start ] || { tail -3 logs/cpnetflux.log; alive gridftp globus-gridftp-server && alive cpnetflux cpnetflux-gridftp-server; }
"
