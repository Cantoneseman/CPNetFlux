#!/bin/bash
# Creates a throwaway CA, GridFTP host/user certificates, a CPNetFlux TLS certificate and token,
# then copies the server half to $SERVER/pki. Keys stay inside the kit directories only.
source "$(dirname "$0")/lib.sh"
set -e
mkdir -p "$PKI"/{ca,host,user,cpn,certificates}
chmod 700 "$PKI"
cd "$PKI"
printf '[req]\ndistinguished_name=dn\n[dn]\n' > openssl.cnf
export OPENSSL_CONF=$PKI/openssl.cnf

if [ ! -f ca/ca.key ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj "/O=CPNetFlux Kit/CN=CPNetFlux Kit CA" \
        -keyout ca/ca.key -out ca/ca.pem -addext "basicConstraints=critical,CA:true" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
fi
sign() {  # dir subject usage [subjectAltName]
    openssl req -newkey rsa:2048 -nodes -subj "$2" -keyout "$1/key.pem" -out "$1/req.csr" 2>/dev/null
    {
        echo "basicConstraints=CA:false"
        echo "keyUsage=critical,digitalSignature,keyEncipherment"
        echo "extendedKeyUsage=$3"
        [ -z "${4:-}" ] || echo "subjectAltName=$4"
    } > "$1/ext.cnf"
    openssl x509 -req -in "$1/req.csr" -CA ca/ca.pem -CAkey ca/ca.key -CAcreateserial -days 30 \
        -extfile "$1/ext.cnf" -out "$1/cert.pem" 2>/dev/null
    chmod 600 "$1/key.pem"
}
if [[ $SERVER_HOST =~ ^[0-9.]+$ ]]; then san="IP:$SERVER_HOST"; else san="DNS:$SERVER_HOST"; fi
sign host "/O=CPNetFlux Kit/CN=host\/$SERVER_HOST" serverAuth,clientAuth
sign user "/O=CPNetFlux Kit/CN=cpnetflux kit user" serverAuth,clientAuth
sign cpn "/O=CPNetFlux Kit/CN=$SERVER_HOST" serverAuth "$san"
ln -sf cert.pem user/usercert.pem
ln -sf key.pem user/userkey.pem
[ -f cpn/token ] || openssl rand -hex 24 > cpn/token
chmod 600 cpn/token

for hash in $(openssl x509 -in ca/ca.pem -noout -subject_hash) $(openssl x509 -in ca/ca.pem -noout -subject_hash_old); do
    cp ca/ca.pem "certificates/$hash.0"
    cat > "certificates/$hash.signing_policy" <<EOF
access_id_CA X509 '/O=CPNetFlux Kit/CN=CPNetFlux Kit CA'
pos_rights globus CA:sign
cond_subjects globus '"/O=CPNetFlux Kit/*"'
EOF
done
server_user=$(on_server 'id -un')
echo "\"$USER_DN\" $server_user" > grid-mapfile
openssl verify -CAfile ca/ca.pem host/cert.pem user/cert.pem cpn/cert.pem

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/pki"
cp -r certificates grid-mapfile "$stage/pki/"
cp host/cert.pem "$stage/pki/hostcert.pem"; cp host/key.pem "$stage/pki/hostkey.pem"
cp cpn/cert.pem "$stage/pki/cpn-cert.pem"; cp cpn/key.pem "$stage/pki/cpn-key.pem"; cp cpn/token "$stage/pki/token"
on_server "rm -rf $SERVER/pki; mkdir -p $SERVER"
copy_to_server "$stage/pki" "$SERVER"
on_server "chmod 700 $SERVER/pki; chmod 600 $SERVER/pki/*key.pem $SERVER/pki/token"
log "certificates ready; grid-mapfile maps the kit user to '$server_user' on the server"
