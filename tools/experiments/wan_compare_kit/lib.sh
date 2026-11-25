# Shared helpers for the comparison kit; sourced by the other scripts.
set -u
KIT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
KIT_CONF=${KIT_CONF:-$KIT_DIR/kit.conf}
[ -f "$KIT_CONF" ] || { echo "error: $KIT_CONF missing; copy kit.conf.example and edit it" >&2; exit 2; }
# shellcheck source=kit.conf.example
source "$KIT_CONF"
: "${SERVER_HOST:?}" "${SERVER_SSH:?}" "${KIT_HOME:?}" "${REMOTE_KIT_HOME:?}"
SSH_OPTS=${SSH_OPTS:-}
NICE=${NICE:-}
CPN_BIN_DIR=${CPN_BIN_DIR:-}
CLIENT=$KIT_HOME/client
SERVER=$REMOTE_KIT_HOME/server
PKI=$CLIENT/pki
GFTP_SUBJECT="/O=CPNetFlux Kit/CN=host/$SERVER_HOST"
USER_DN="/O=CPNetFlux Kit/CN=cpnetflux kit user"

log() { echo "[$(date +%T)] $*"; }
die() { echo "error: $*" >&2; exit 1; }
nice_prefix() { [ -n "$NICE" ] && echo "nice -n $NICE"; }

# Runs a bash snippet on the server host.
on_server() {
    if [ "$SERVER_SSH" = local ]; then
        bash -c "$1"
    else
        # shellcheck disable=SC2086
        ssh $SSH_OPTS -o BatchMode=yes "$SERVER_SSH" "bash -c $(printf %q "$1")"
    fi
}

# copy_to_server <local path>... <remote dir>
copy_to_server() {
    local dest=${!#} count=$(($# - 1))
    on_server "mkdir -p $(printf %q "$dest")"
    if [ "$SERVER_SSH" = local ]; then
        cp -r "${@:1:$count}" "$dest/"
    else
        # shellcheck disable=SC2086
        scp -q $SSH_OPTS -o BatchMode=yes -r "${@:1:$count}" "$SERVER_SSH:$dest/"
    fi
}

# Directory holding the CPNetFlux binaries on either end.
client_bin() { [ -n "$CPN_BIN_DIR" ] && echo "$CPN_BIN_DIR" || cat "$CLIENT/cpn-bin-dir" 2>/dev/null; }
server_bin() { [ -n "$CPN_BIN_DIR" ] && echo "$CPN_BIN_DIR" || on_server "cat $SERVER/cpn-bin-dir 2>/dev/null"; }

# Refuses to delete anything that is not strictly inside the kit homes.
assert_kit_path() {
    case "$1" in
        "$KIT_HOME"/?*|"$REMOTE_KIT_HOME"/?*) ;;
        *) die "refusing to touch $1: not inside KIT_HOME or REMOTE_KIT_HOME" ;;
    esac
}

gridftp_env() {
    export X509_CERT_DIR=$PKI/certificates X509_USER_CERT=$PKI/user/usercert.pem
    export X509_USER_KEY=$PKI/user/userkey.pem X509_USER_PROXY=$PKI/x509up
    export OPENSSL_CONF=$PKI/openssl.cnf
    # Globus 10.x on OpenSSL 3 writes a post-handshake TLS 1.3 record into downloaded files.
    export GLOBUS_GSSAPI_MAX_TLS_PROTOCOL=TLS1_2_VERSION
}
