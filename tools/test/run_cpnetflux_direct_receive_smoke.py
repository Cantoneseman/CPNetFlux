#!/usr/bin/env python3
"""Real TCP, mapped sinks, cross-window CRC and negative acknowledgement gates."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading

from run_cpnetflux_tcp_frame_bench_smoke import arguments, exact, free_port, result, start_server, stop


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    binary = args.build_dir / 'cpnetflux-tcp-frame-bench'
    successes, failures = [], []
    with tempfile.TemporaryDirectory(prefix='cpnetflux-direct-smoke-') as directory:
        for sink in ('mapped_copy', 'mapped_direct'):
            for lanes, size in ((1, 1), (4, 4 * 1024**2 + 37), (1, 64 * 1024**2 + 65537)):
                port = free_port()
                command = arguments(binary, 'server', port, lanes, size, 'v3', 'crc32c') + [sink, directory]
                server = start_server(command)
                try:
                    client = subprocess.run(arguments(binary, 'client', port, lanes, size, 'v3', 'crc32c'),
                                            capture_output=True, text=True, timeout=30)
                    out, error = server.communicate(timeout=30)
                    assert client.returncode == server.returncode == 0, (client.stderr, error)
                    row = result(out)
                    assert row['diagnostic_only'] and row['file_io'] and not row['complete_v3_task']
                    assert row['sink'] == sink and row['readback_verified'] and row['receiver_acknowledged']
                    assert row['logical_bytes'] == row['lease_io_bytes'] == size
                    assert row['map_calls'] == row['unmap_calls'] and row['active_mapped_bytes'] == 0
                    assert row['peak_mapped_bytes_sum'] <= lanes * 64 * 1024**2
                    assert row['mapped_copy_bytes'] == (size if sink == 'mapped_copy' else 0)
                    assert row['direct_receive_bytes'] == (size if sink == 'mapped_direct' else 0)
                    assert row['process_wall_seconds'] >= row['elapsed_seconds'] > 0
                    successes.append({'sink': sink, 'lanes': lanes, 'bytes': size})
                finally:
                    stop(server)
                assert not list(Path(directory).iterdir()), 'owned target leaked'

            # Forward FILE_BEGIN and one DATA, with an intentional wire fault.
            for fault in ('crc', 'truncated_prefix', 'truncated_payload', 'offset', 'identity', 'attempt'):
                port, size = free_port(), 256 * 1024 + 13
                server = start_server(arguments(binary, 'server', port, 1, size, 'v3', 'crc32c') + [sink, directory])
                errors = []
                with socket.socket() as listener:
                    listener.bind(('127.0.0.1', 0))
                    listener.listen(1)
                    listener.settimeout(10)
                    proxy_port = listener.getsockname()[1]

                    def proxy():
                        try:
                            with listener.accept()[0] as client, socket.create_connection(('127.0.0.1', port), 10) as peer:
                                client.settimeout(10)
                                peer.settimeout(10)
                                client.sendall(exact(peer, 1))
                                begin = exact(client, 64)
                                peer.sendall(begin + exact(client, struct.unpack_from('!I', begin, 32)[0]))
                                header = exact(client, 64)
                                payload = exact(client, struct.unpack_from('!I', header, 32)[0])
                                if fault == 'crc':
                                    payload[4] ^= 1
                                elif fault == 'offset':
                                    struct.pack_into('!Q', header, 24, 1)
                                elif fault == 'identity':
                                    struct.pack_into('!I', header, 12, 999)
                                elif fault == 'attempt':
                                    struct.pack_into('!Q', header, 56, 99)
                                if fault == 'truncated_prefix':
                                    payload = payload[:2]
                                elif fault == 'truncated_payload':
                                    payload = payload[:100]
                                peer.sendall(header + payload)
                                peer.shutdown(socket.SHUT_WR)
                                assert peer.recv(64) == b'', 'unexpected success acknowledgement'
                        except Exception as error:
                            errors.append(repr(error))

                    worker = threading.Thread(target=proxy)
                    worker.start()
                    try:
                        client = subprocess.run(arguments(binary, 'client', proxy_port, 1, size, 'v3', 'crc32c'),
                                                capture_output=True, text=True, timeout=20)
                        out, error = server.communicate(timeout=20)
                        worker.join(timeout=10)
                        assert not worker.is_alive() and not errors, errors
                        assert server.returncode != 0 and client.returncode != 0
                        assert 'receiver_acknowledged' not in out + client.stdout
                        if fault in ('offset', 'identity', 'attempt'):
                            assert 'DATA preflight rejected' in error, error
                            assert 'maps_before_failure=0' in error, error
                        elif fault == 'crc':
                            assert 'DATA checksum mismatch' in error, error
                        failures.append({'sink': sink, 'fault': fault})
                    finally:
                        stop(server)
                assert not list(Path(directory).iterdir()), 'failed target leaked'
        for sink, lanes, size, mode, crc in (('invalid', 1, 1, 'v3', 'crc32c'),
                                            ('mapped_direct', 5, 8, 'v3', 'crc32c'),
                                            ('mapped_direct', 1, 8*1024**3+1, 'v3', 'crc32c'),
                                            ('mapped_direct', 1, 1, 'raw', 'none'),
                                            ('mapped_direct', 1, 1, 'v3', 'none')):
            invalid = subprocess.run(arguments(binary, 'server', 1, lanes, size, mode, crc) + [sink, directory],
                                     capture_output=True, text=True, timeout=5)
            assert invalid.returncode != 0 and not invalid.stdout
    print(json.dumps({'successes': successes, 'wire_faults_rejected': failures, 'cleanup': True}))


if __name__ == '__main__':
    main()
