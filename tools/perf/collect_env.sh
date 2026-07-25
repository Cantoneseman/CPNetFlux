#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 --output <path>" >&2
}

output=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            if [[ $# -lt 2 ]]; then
                usage
                exit 2
            fi
            output="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "$output" ]]; then
    usage
    exit 2
fi

mkdir -p "$(dirname "$output")"

run_block() {
    local title="$1"
    shift
    {
        echo
        echo "## ${title}"
        echo
        echo '```text'
        if command -v "$1" >/dev/null 2>&1; then
            "$@" 2>&1 || true
        else
            echo "$1: not available"
        fi
        echo '```'
    } >>"$output"
}

append_file_or_note() {
    local title="$1"
    local file="$2"
    {
        echo
        echo "## ${title}"
        echo
        echo '```text'
        if [[ -r "$file" ]]; then
            cat "$file"
        else
            echo "${file}: not available"
        fi
        echo '```'
    } >>"$output"
}

primary_interfaces() {
    ip -o -4 addr show scope global 2>/dev/null | awk '{print $2}' | sort -u || true
}

{
    echo "# GridFlux Environment Snapshot"
    echo
    echo "- timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- hostname: $(hostname)"
    echo "- kernel: $(uname -a)"
} >"$output"

run_block "OS Release" cat /etc/os-release
run_block "CPU" lscpu
run_block "Memory" free -h
run_block "IP Addresses" ip addr
append_file_or_note "TCP sysctl" /proc/sys/net/ipv4/tcp_congestion_control

{
    echo
    echo "## TCP Details"
    echo
    echo '```text'
    for key in \
        net.ipv4.tcp_congestion_control \
        net.ipv4.tcp_rmem \
        net.ipv4.tcp_wmem \
        net.ipv4.tcp_window_scaling \
        net.ipv4.tcp_timestamps \
        net.core.rmem_max \
        net.core.wmem_max \
        net.core.somaxconn; do
        sysctl "$key" 2>/dev/null || echo "${key}: not available"
    done
    echo '```'
} >>"$output"

run_block "iperf3" iperf3 --version
run_block "fio" fio --version
run_block "numactl" numactl --hardware

{
    echo
    echo "## ethtool"
} >>"$output"

interfaces="$(primary_interfaces)"
if [[ -z "$interfaces" ]]; then
    {
        echo
        echo '```text'
        echo "no global IPv4 interface found"
        echo '```'
    } >>"$output"
else
    while IFS= read -r iface; do
        [[ -z "$iface" ]] && continue
        run_block "ethtool ${iface}" ethtool "$iface"
    done <<<"$interfaces"
fi

echo "wrote ${output}"
