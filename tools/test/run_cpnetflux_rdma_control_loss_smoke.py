#!/usr/bin/env python3
"""Control EOF must wake an active native RDMA directory receiver."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import threading
import time

from tree_smoke_common import free_port


class ControlProxy:
    def __init__(self, bind, port, backend):
        self.backend = backend
        self.listener = socket.socket()
        self.listener.bind((bind, port))
        self.listener.listen(1)
        self.client = None
        self.server = None
        self.threads = []
        self.acceptor = threading.Thread(target=self.accept, daemon=True)
        self.acceptor.start()

    def accept(self):
        try:
            self.client, _ = self.listener.accept()
            self.server = socket.create_connection(self.backend, timeout=5)
            self.server.settimeout(None)
            for source, target in [(self.client, self.server), (self.server, self.client)]:
                thread = threading.Thread(target=self.relay, args=(source, target), daemon=True)
                self.threads.append(thread)
                thread.start()
        except OSError:
            return

    @staticmethod
    def relay(source, target):
        try:
            while block := source.recv(65536):
                target.sendall(block)
        except OSError:
            pass

    def close_client(self):
        assert self.client is not None
        self.client.shutdown(socket.SHUT_RDWR)

    def close(self):
        for stream in [self.client, self.server, self.listener]:
            if stream is not None:
                try:
                    stream.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                stream.close()
        self.acceptor.join(timeout=1)
        for thread in self.threads:
            thread.join(timeout=1)


def run(build, bind, root):
    source = root / 'source' / 'payload'
    source.mkdir(parents=True)
    block = bytes(range(256)) * 4096
    with (source / 'payload.bin').open('wb') as stream:
        for _ in range(256):
            stream.write(block)
    control_port, proxy_port, data_port = free_port(), free_port(), free_port()
    server = client = proxy = None
    try:
        env = os.environ.copy()
        env['CPNETFLUX_V3_PROTOCOL_TRACE'] = '1'
        with (root / 'server.log').open('w') as log:
            server = subprocess.Popen([
                str(build / 'cpnetflux-gridftp-server'), '--host', bind,
                '--port', str(control_port), '--data-port-base', str(data_port),
                '--root', str(source.parent), '--connections', '1',
                '--data-transport', 'rdma', '--checksum', 'crc32c',
            ], stdout=log, stderr=subprocess.STDOUT, env=env)
        until = time.monotonic() + 10
        while True:
            try:
                with socket.create_connection((bind, control_port), timeout=.1):
                    break
            except OSError:
                assert server.poll() is None, (root / 'server.log').read_text()
                if time.monotonic() >= until:
                    raise TimeoutError('temporary server startup')
                time.sleep(.01)
        proxy = ControlProxy(bind, proxy_port, (bind, control_port))
        with (root / 'client.log').open('w') as log:
            client = subprocess.Popen([
                str(build / 'cpnetflux-tree-download-client'), '--host', bind,
                '--port', str(proxy_port), '--local-bind-address', bind,
                '--source-dir', 'payload', '--dest-dir', str(root / 'download'),
                '--connections', '1', '--file-parallelism', '1',
                '--control-reuse', 'worker', '--data-session-reuse', 'tree',
                '--file-scheduling', 'dynamic', '--data-transport', 'rdma',
                '--checksum', 'crc32c', '--reconnect-attempts', '0',
                '--transfer-timeout-ms', '15000',
                '--json-summary', str(root / 'summary.json'),
            ], stdout=log, stderr=subprocess.STDOUT, env=env)
        until = time.monotonic() + 10
        while True:
            trace = (root / 'client.log').read_text()
            if '"event":"file_begin_read"' in trace:
                assert '"event":"file_end_read"' not in trace, 'missed injection window'
                break
            assert client.poll() is None, trace
            if time.monotonic() >= until:
                raise TimeoutError('RDMA FILE_BEGIN not observed')
            time.sleep(.002)
        os.kill(server.pid, signal.SIGSTOP)
        time.sleep(.03)
        started = time.monotonic()
        proxy.close_client()
        result = client.wait(timeout=5)
        elapsed = time.monotonic() - started
        output = (root / 'client.log').read_text()
        assert result != 0, output
        assert 'control connection closed' in output, output
        assert 'total deadline exceeded' not in output, output
        assert not (root / 'download' / 'payload.bin').exists(), 'unexpected completed file'
        summary = json.loads((root / 'summary.json').read_text())
        assert summary['data_transport'] == 'rdma', summary
        record = {'result': 'pass', 'fault': 'control_eof_with_rdma_peer_paused',
                  'client_exit': result, 'seconds_after_fault': elapsed,
                  'task_deadline_seconds': 15, 'summary': summary}
        (root / 'acceptance.json').write_text(json.dumps(record, indent=2) + '\n')
        print(json.dumps({'result': 'pass', 'seconds_after_fault': elapsed}))
    finally:
        if server is not None and server.poll() is None:
            os.kill(server.pid, signal.SIGCONT)
        for process in [client, server]:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        if proxy is not None:
            proxy.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--bind-address', default=os.environ.get('CPNETFLUX_TEST_RDMA_BIND', ''))
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    if not args.bind_address:
        print('SKIP: set CPNETFLUX_TEST_RDMA_BIND')
        return 77
    if args.evidence_dir:
        args.evidence_dir.mkdir(parents=True, exist_ok=False)
        run(args.build_dir.resolve(), args.bind_address, args.evidence_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='cpnetflux-rdma-control-loss-') as temporary:
            run(args.build_dir.resolve(), args.bind_address, Path(temporary))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
