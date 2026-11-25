#!/bin/bash
# bench.sh [run_name]: GridFTP vs CPNetFlux (no compression / adaptive) over SERVER_HOST.
# Results go to $KIT_HOME/client/results/<run_name>; every transfer is checked with sha256.
source "$(dirname "$0")/lib.sh"
run=${1:-$(date +%Y%m%d-%H%M%S)}
out=$CLIENT/results/$run
[ -e "$out/results.csv" ] && die "$out already has results; pick another run name"
mkdir -p "$out"
# Two runs at once share the link and delete each other's remote directories.
exec 9>"$CLIENT/bench.lock"
flock -n 9 || die "another bench.sh is running"

read -r -a datasets <<< "$DATASETS"
read -r -a tools <<< "$TOOLS"
read -r -a directions <<< "$DIRECTIONS"
data=$CLIENT/data
back=$CLIENT/back
cbin=$(client_bin)
[ -n "$cbin" ] || die "no CPNetFlux build on the client; run build_cpnetflux.sh"
gridftp_env
gftp_url=gsiftp://$SERVER_HOST:$GFTP_PORT
cpn_common=(--host "$SERVER_HOST" --port "$CPN_PORT" --file-parallelism "$PARALLEL_STREAMS"
    --checksum crc32c --auth-mode token --auth-token-file "$PKI/cpn/token" --tls-mode required
    --tls-ca-file "$PKI/ca/ca.pem" --tls-server-name "$SERVER_HOST" --data-transport tcp
    --transfer-timeout-ms $((TRANSFER_TIMEOUT * 1000)))
iface=$(ip route get "$SERVER_HOST" 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -1)

log() { echo "[$(date +%T)] $*" | tee -a "$out/progress.log"; }
nic_bytes() { cat "/sys/class/net/$iface/statistics/$1_bytes" 2>/dev/null || echo 0; }

make_data() {
    local stamp="$BIG_MIB $SMALL_COUNT $SMALL_KIB"
    [ "$(cat "$data/.params" 2>/dev/null)" = "$stamp" ] && return
    assert_kit_path "$data"
    rm -rf "$data"; mkdir -p "$data"/{big_text,big_random,small_text}
    python3 - "$data" "$BIG_MIB" "$SMALL_COUNT" "$SMALL_KIB" <<'EOF'
import os, random, sys
root, big_mib, small_count, small_kib = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
rng = random.Random(20261124)
def csv_block(n):
    rows, size = [], 0
    while size < n:
        row = "%d,2026-11-%02dT%02d:%02d:%02d,sensor-%03d,%.4f,%.4f,%d,%s\n" % (
            rng.randrange(10**12), rng.randint(1, 30), rng.randint(0, 23), rng.randint(0, 59),
            rng.randint(0, 59), rng.randrange(500), rng.uniform(-90, 90), rng.uniform(-180, 180),
            rng.randrange(65536), rng.choice(["OK", "WARN", "ERROR", "RETRY"]))
        rows.append(row); size += len(row)
    return "".join(rows).encode()[:n]
with open(os.path.join(root, "big_text", "data.csv"), "wb") as f:
    for _ in range(big_mib):
        f.write(csv_block(1 << 20))
with open(os.path.join(root, "big_random", "data.bin"), "wb") as f:
    for _ in range(big_mib):
        f.write(rng.randbytes(1 << 20))
for i in range(small_count):
    with open(os.path.join(root, "small_text", "part-%05d.csv" % i), "wb") as f:
        f.write(csv_block(small_kib << 10))
EOF
    for ds in big_text big_random small_text; do
        (cd "$data/$ds" && find . -type f | sort | xargs sha256sum) > "$data/$ds.sha256"
    done
    echo "$stamp" > "$data/.params"
}

remote_dir() { case $1 in gridftp) echo "$SERVER/gftp-root/$2" ;; *) echo "$SERVER/cpn-root/$2" ;; esac; }
verify_remote() {  # tool ds
    on_server "cd $(remote_dir "$1" "$2") 2>/dev/null && find . -type f ! -name '*.cpnetflux*' | sort | xargs -r sha256sum" \
        | cmp -s - "$data/$2.sha256"
}
verify_local() { (cd "$back/$1" 2>/dev/null && find . -type f ! -name '*.cpnetflux*' | sort | xargs -r sha256sum) | cmp -s - "$data/$1.sha256"; }

gftp_flags() {  # direction ds
    local big="-fast -p $PARALLEL_STREAMS" small="-fast -p 1 -cc $SMALL_CONCURRENCY"
    # Extended block mode makes the sender open the data connections; behind NAT the server
    # cannot reach the client, so downloads fall back to stream mode.
    if [ "$1" = down ] && [ "$CLIENT_BEHIND_NAT" = 1 ]; then big="-cc 1"; small="-cc $SMALL_CONCURRENCY"; fi
    case $2 in small_*) echo "$small" ;; *) echo "$big" ;; esac
}

transfer() {  # tool ds direction log json
    local tool=$1 ds=$2 dir=$3 rdir; rdir=$(remote_dir "$tool" "$ds")
    if [ "$tool" = gridftp ]; then
        # Recursive transfers stall or fail ("Bad MLSD response") with Globus 10.x; list the files.
        local list=$out/urls_${ds}_$dir.txt
        (cd "$data/$ds" && find . -type f | sort | sed 's#^\./##') | while read -r f; do
            if [ "$dir" = up ]; then echo "file://$data/$ds/$f $gftp_url$rdir/$f"
            else echo "$gftp_url$rdir/$f file://$back/$ds/$f"; fi
        done > "$list"
        # shellcheck disable=SC2046
        timeout -k 30 "$TRANSFER_TIMEOUT" globus-url-copy -ss "$GFTP_SUBJECT" $(gftp_flags "$dir" "$ds") -cd -f "$list" > "$4" 2>&1
    else
        local flags=(--compression off)
        [ "$tool" = cpn_adaptive ] && flags=(--compression auto --compression-policy adaptive)
        if [ "$dir" = up ]; then
            timeout -k 30 "$TRANSFER_TIMEOUT" "$cbin/cpnetflux-tree-upload-client" "${cpn_common[@]}" "${flags[@]}" \
                --source-dir "$data/$ds" --dest-dir "$ds" --json-summary "$5" > "$4" 2>&1
        else
            timeout -k 30 "$TRANSFER_TIMEOUT" "$cbin/cpnetflux-tree-download-client" "${cpn_common[@]}" "${flags[@]}" \
                --source-dir "$ds" --dest-dir "$back/$ds" --json-summary "$5" > "$4" 2>&1
        fi
    fi
}

prepare() {  # tool ds direction
    local rdir; rdir=$(remote_dir "$1" "$2")
    if [ "$3" = up ]; then
        on_server "rm -rf $rdir; mkdir -p $rdir"
    else
        assert_kit_path "$back/$2"
        rm -rf "${back:?}/$2"; mkdir -p "$back/$2"
        # A download needs a verified copy on the server; stage it untimed if the upload is missing.
        verify_remote "$1" "$2" || { on_server "rm -rf $rdir; mkdir -p $rdir"; transfer "$1" "$2" up "$out/stage_$1_$2.log" "$out/stage_$1_$2.json"; }
        verify_remote "$1" "$2"
    fi
}

run_one() {  # rep tool ds direction
    local rep=$1 tool=$2 ds=$3 dir=$4 tag="$2_$3_$4_r$1" rc=skipped ok=no
    if prepare "$tool" "$ds" "$dir"; then
        local tx0 rx0 start end; tx0=$(nic_bytes tx); rx0=$(nic_bytes rx); start=$(date +%s.%N)
        transfer "$tool" "$ds" "$dir" "$out/$tag.log" "$out/$tag.json"; rc=$?
        end=$(date +%s.%N)
        if [ $rc -eq 0 ]; then
            if [ "$dir" = up ]; then verify_remote "$tool" "$ds" && ok=yes; else verify_local "$ds" && ok=yes; fi
        fi
        local bytes secs mbps; bytes=$(du -sb --apparent-size "$data/$ds" | cut -f1)
        secs=$(python3 -c "print(f'{$end - $start:.3f}')")
        mbps=$(python3 -c "print(f'{$bytes * 8 / 1e6 / ($end - $start):.1f}')")
        echo "$rep,$tool,$ds,$dir,$rc,$ok,$bytes,$secs,$mbps,$iface,$(( $(nic_bytes tx) - tx0 )),$(( $(nic_bytes rx) - rx0 ))" >> "$out/results.csv"
        log "$tag rc=$rc verified=$ok ${secs}s ${mbps}Mbit/s"
    else
        echo "$rep,$tool,$ds,$dir,no_source,no,,,,$iface,," >> "$out/results.csv"
        log "$tag skipped: no verified copy on the server"
    fi
}

iperf_probe() {  # label
    command -v iperf3 >/dev/null || return 0
    on_server "command -v iperf3 >/dev/null" || return 0
    for flags in "" "-R" "-P $PARALLEL_STREAMS" "-R -P $PARALLEL_STREAMS"; do
        on_server "cd $SERVER && iperf3 -s -B $SERVER_HOST -p $IPERF_PORT -1 -D --pidfile run/iperf.pid"
        sleep 1
        local mbps
        # shellcheck disable=SC2086
        mbps=$(iperf3 -c "$SERVER_HOST" -p "$IPERF_PORT" -t 10 $flags -J 2>/dev/null | python3 -c '
import json, sys
end = json.load(sys.stdin)["end"]
print("%.1f" % ((end.get("sum_received") or end["sum"])["bits_per_second"] / 1e6))' 2>/dev/null)
        echo "$1,${flags:-single},${mbps:-FAILED}" >> "$out/iperf.csv"
        log "iperf $1 ${flags:-single}: ${mbps:-FAILED} Mbit/s"
    done
}

grid-proxy-info -exists -valid 4:00 2>/dev/null || grid-proxy-init -q -valid 12:00 -out "$X509_USER_PROXY" || die "grid-proxy-init failed"
log "proxy timeleft=$(grid-proxy-info -timeleft)s"
"$KIT_DIR/servers.sh" status | grep -q 'cpnetflux: running' || die "servers not running; run servers.sh start"
make_data
log "data ready: $(du -sh "$data" | cut -f1) (big ${BIG_MIB} MiB, ${SMALL_COUNT} x ${SMALL_KIB} KiB)"
{
    echo "client_host=$(hostname) server=$SERVER_SSH server_host=$SERVER_HOST"
    echo "cpn_commit=$(cat "$CLIENT/cpn-commit" 2>/dev/null || echo "$CPN_BIN_DIR")"
    echo "client_bins:"; sed 's/^/  /' "$CLIENT/build-client.sha256" 2>/dev/null
    echo "server_bins:"; sed 's/^/  /' "$CLIENT/build-server.sha256" 2>/dev/null
    echo "client_route=$(ip route get "$SERVER_HOST" 2>/dev/null | head -1)"
    echo "globus_url_copy=$(globus-url-copy -version 2>&1 | head -1)"
    echo "config:"; grep -E '^[A-Z_]+=' "$KIT_CONF" | sed 's/^/  /'
} > "$out/env.txt"
echo "rep,tool,dataset,direction,rc,verified,bytes,seconds,mbit_per_s,nic,nic_tx_bytes,nic_rx_bytes" > "$out/results.csv"
iperf_probe before
for rep in $(seq 1 "$REPS"); do
    # Rotate the tool order each repetition so no tool always runs first.
    order=("${tools[@]:$(( (rep - 1) % ${#tools[@]} ))}" "${tools[@]:0:$(( (rep - 1) % ${#tools[@]} ))}")
    for ds in "${datasets[@]}"; do
        for dir in "${directions[@]}"; do
            for tool in "${order[@]}"; do run_one "$rep" "$tool" "$ds" "$dir"; done
        done
    done
done
iperf_probe after
python3 "$KIT_DIR/summarize.py" "$out" | tee "$out/summary.md"
log "done: $out"
