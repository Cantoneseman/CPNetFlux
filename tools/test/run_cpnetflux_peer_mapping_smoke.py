#!/usr/bin/env python3
"""Prove destination selection with two real TCP relays and lane-local faults."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_lane_recovery_smoke import make_tls_files
from run_cpnetflux_lane_recovery_interop import wait_for_tcp
from run_v3_logical_link_matrix import make_dataset
from tree_smoke_common import free_port, tree_hash, stop_server


def run_case(build, root, direction, fault=None):
    case = root / (direction + '-' + (fault or 'normal'))
    case.mkdir()
    source, server_root = case / 'source', case / 'server-root'
    make_dataset(source, 2)
    server_root.mkdir()
    if direction == 'download': shutil.copytree(source, server_root / 'dataset')
    expected = tree_hash(source)
    control, data_base = free_port(), clamp_passive_data_port_base(free_port())
    cert, key = make_tls_files(case)
    command = [str(build / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
        '--port', str(control), '--data-port-base', str(data_base), '--root', str(server_root),
        '--connections', '2', '--checksum', 'crc32c', '--link-set-max-links', '2',
        '--tls-mode', 'required' if not fault else 'off', '--tls-cert-file', str(cert), '--tls-key-file', str(key),
        '--data-tls-mode', 'required' if not fault else 'off']
    proxies = []
    with (case / 'server.log').open('w') as output:
        server = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    try:
        wait_for_tcp('127.0.0.1', control)
        for index, address in enumerate(('127.0.0.2', '127.0.0.3')):
            proxies.append(FaultProxy(address, '127.0.0.1', control, data_base,
                drops=int(index == 1 and fault in ('data-reset', 'control-reset')), drop_bytes=512*1024,
                delay=0.0001, target_data_connection=1,
                drop_control_after_data_connection=1 if index == 1 and fault == 'control-reset' else None,
                log=case / ('proxy-' + str(index) + '.jsonl')))
        peers = ['127.0.0.2', '127.0.0.4' if fault == 'unreachable' else '127.0.0.3']
        tls_args = (['--tls-mode', 'required', '--tls-ca-file', str(cert), '--tls-server-name', 'localhost', '--data-tls-mode', 'required'] if not fault else ['--tls-mode', 'off', '--data-tls-mode', 'off'])
        summary_path = case / 'summary.json'
        command = [str(build / ('cpnetflux-tree-' + direction + '-client')),
            '--host', 'localhost', '--port', str(control), '--peer-addresses', ','.join(peers),
            '--local-bind-addresses', '127.0.0.1', '--connections', '2',
            '--file-parallelism', '2', '--link-set-links', '2', '--checksum', 'crc32c',
            '--source-dir', str(source) if direction == 'upload' else 'dataset',
            '--dest-dir', 'uploaded' if direction == 'upload' else str(case / 'downloaded'),
            '--data-pending-window', '2', '--control-pipeline-depth', '2',
            '--reconnect-attempts', '2' if fault in ('data-reset', 'control-reset') else '0',
            '--transfer-timeout-ms', '30000', '--phase-timing', 'on',
            *tls_args,
            '--json-summary', str(summary_path)]
        # TLS identity stays localhost despite mapped destination IPs.
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        (case / 'client.log').write_text(result.stdout + result.stderr)
        summary = json.loads(summary_path.read_text())
        assert summary['peer_addresses'] == peers, summary
        if fault == 'unreachable':
            assert result.returncode != 0 and summary['result'] == 'fail', summary
        else:
            assert result.returncode == 0 and summary['result'] == 'pass', result.stderr
            assert summary['actual_transport'] == 'v3'
            assert summary['control_tls_verified'] == (fault is None) and summary['data_tls_verified'] == (fault is None)
            endpoints = summary['lane_endpoints']
            assert len(endpoints) == 2, endpoints
            for index, endpoint in enumerate(endpoints):
                assert endpoint['channel_index'] == endpoint['lane_index'] == index, endpoint
                assert endpoint['control_peer_address'] == endpoint['data_peer_address'] == peers[index], endpoint
                assert endpoint['control_local_address'] == endpoint['data_local_address'] == '127.0.0.1', endpoint
            observed = tree_hash(server_root / 'uploaded' if direction == 'upload' else case / 'downloaded')
            assert observed == expected, (observed, expected)
            if fault:
                assert summary['lane_local_recovery_count'] >= 1, summary
                assert endpoints[1]['data_connections'] >= 2, endpoints
                records = [json.loads(x) for x in (case / 'proxy-1.jsonl').read_text().splitlines()]
                assert any(x['event'] in ('reset', 'control_reset') for x in records), records
        record = dict(case=case.name, result='pass', expected_failure=fault == 'unreachable',
            client_exit=result.returncode, tree=expected, summary=summary)
        (case / 'result.json').write_text(json.dumps(record, indent=2)+'\n')
        return record
    finally:
        for proxy in proxies: proxy.close()
        stop_server(server, case / 'server.log')
        key.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpnetflux-peer-map-') as temporary:
        root = args.evidence_dir or Path(temporary)
        if args.evidence_dir: root.mkdir(parents=True, exist_ok=False)
        records = [run_case(args.build_dir.absolute(), root, direction, fault)
            for direction, fault in [('upload', None), ('download', None),
                ('upload', 'data-reset'), ('download', 'control-reset'), ('upload', 'unreachable')]]
        (root / 'result.json').write_text(json.dumps(dict(result='pass', cases=records), indent=2)+'\n')
        print(json.dumps(dict(result='pass', cases=len(records))))


if __name__ == '__main__': main()
