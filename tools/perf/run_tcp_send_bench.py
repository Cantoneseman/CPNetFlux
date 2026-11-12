#!/usr/bin/env python3
"""Single-writer bounded two-host TCP-send diagnostic; no runtime integration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import select
import shlex
import socket
import statistics
import subprocess
import time

PROJECT = Path('/home/Su/projects/CPNetFlux')
BINARY = PROJECT / 'build/cpnetflux-tcp-send-bench'
SCRIPT = PROJECT / 'tools/perf/run_tcp_send_bench.py'
MARKER = 'cpnetflux-tcp-send-32-owned-v1'
MIN_MEMORY = 12 * 1024**3
INTERFACE = 'eno17095np0'


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024**2), b''):
            digest.update(block)
    return digest.hexdigest()


def vm(pid):
    try:
        fields = dict(line.split(':', 1) for line in Path(f'/proc/{pid}/status').read_text().splitlines() if ':' in line)
        return {k: int(fields.get(k, '0 kB').split()[0]) * 1024 for k in ('VmRSS', 'VmSwap')}
    except (FileNotFoundError, ProcessLookupError):
        return {'VmRSS': 0, 'VmSwap': 0}


def snapshot(which):
    mem = int(next(l.split()[1] for l in Path('/proc/meminfo').read_text().splitlines() if l.startswith('MemAvailable:'))) * 1024
    swap = int(next(l.split()[1] for l in Path('/proc/vmstat').read_text().splitlines() if l.startswith('pswpout ')))
    pid, ticks = (126766, 1191040) if which == 1 else (164834, 1679976)
    actual = int(Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()[19])
    assert actual == ticks, ('production identity changed', pid, actual)
    names = ('rx_bytes', 'tx_bytes', 'rx_errors', 'tx_errors', 'rx_dropped', 'tx_dropped', 'rx_missed_errors', 'rx_crc_errors')
    net = {n: int((Path('/sys/class/net') / INTERFACE / 'statistics' / n).read_text()) for n in names}
    return {'mem_available_bytes': mem, 'pswpout': swap, 'production_pid': pid,
            'production_start_ticks': ticks, 'net': net}


def begin_guard(which):
    state = {'before': snapshot(which), 'min_mem_available_bytes': 2**63,
             'owned_swap_peak_bytes': 0, 'owned_rss_peak_bytes': 0, 'samples': 0}
    observe(which, [], state)
    return state


def observe(which, pids, state):
    snap = snapshot(which)
    state['min_mem_available_bytes'] = min(state['min_mem_available_bytes'], snap['mem_available_bytes'])
    values = [vm(pid) for pid in set(pids + [os.getpid()])]
    state['owned_swap_peak_bytes'] = max(state['owned_swap_peak_bytes'], sum(v['VmSwap'] for v in values))
    state['owned_rss_peak_bytes'] = max(state['owned_rss_peak_bytes'], sum(v['VmRSS'] for v in values))
    state['samples'] += 1
    assert snap['mem_available_bytes'] >= MIN_MEMORY, 'MemAvailable below12GiB'
    assert state['owned_swap_peak_bytes'] == 0, 'owned swap detected'
    assert snap['pswpout'] == state['before']['pswpout'], 'host swap-out detected'
    state['after'] = snap


def owned(root):
    assert root.is_absolute() and root.parent == Path('/dev/shm')
    assert root.name.startswith('cpnetflux-tcp-send-32-') and not root.is_symlink()
    assert (root / 'MARKER').is_file() and not (root / 'MARKER').is_symlink()
    assert (root / 'MARKER').read_text() == MARKER
    return root


def create(root):
    assert root.parent == Path('/dev/shm') and root.name.startswith('cpnetflux-tcp-send-32-')
    root.mkdir()
    (root / 'MARKER').write_text(MARKER)
    return owned(root)


def cleanup(root):
    owned(root)
    files = list(root.iterdir())
    assert all(p.name in ('MARKER', 'source.bin', 'target.bin') and p.is_file() and not p.is_symlink() for p in files)
    for path in files:
        path.unlink()
    root.rmdir()
    return {'removed': not root.exists()}


def capability(commit, which):
    assert output('git', '-C', str(PROJECT), 'rev-parse', 'HEAD') == commit
    assert not output('git', '-C', str(PROJECT), 'diff', '--name-only', 'HEAD'), 'tracked dirty source'
    with socket.socket() as sock:
        sock.setsockopt(socket.SOL_SOCKET, 60, 1)
        assert sock.getsockopt(socket.SOL_SOCKET, 60) == 1
    from run_small_file_baseline import receipt
    config = receipt(commit)
    config['diagnostic_sha256'] = sha(BINARY)
    return {'receipt': config, 'kernel': output('uname', '-r'),
            'memlock': list(resource.getrlimit(resource.RLIMIT_MEMLOCK)), 'so_zerocopy': True,
            'route': output('ip', 'route', 'get', f'192.168.100.{3-which}', 'from', f'192.168.100.{which}'),
            'features': output('ethtool', '-k', INTERFACE), 'snapshot': snapshot(which)}


def command(role, which, port, size, mode, path):
    return [str(BINARY), role, f'192.168.100.{which}', f'192.168.100.{3-which}',
            str(port), '4', str(size), mode, str(path), '30000']


def clean_env():
    return {k: v for k, v in os.environ.items() if not k.startswith('CPNETFLUX_') and k != 'LD_PRELOAD'}


def peer(args, action, **values):
    cmd = ['python3', str(SCRIPT), '--action', action, '--commit', args.commit,
           '--evidence', str(args.evidence), '--root', str(args.root), '--which', '2']
    for key, value in values.items():
        cmd.extend(['--' + key.replace('_', '-'), str(value)])
    return ['ssh', '-n', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15', 'r770-2', shlex.join(cmd)]


def remote(args, action, **values):
    if action != 'prepare':
        return json.loads(subprocess.check_output(peer(args, action, **values), text=True, timeout=90))
    state = begin_guard(1)
    process = subprocess.Popen(peer(args, action, **values), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    started = time.monotonic()
    try:
        while process.poll() is None:
            observe(1, [], state)
            assert time.monotonic()-started < 90, 'source preparation deadline'
            time.sleep(.01)
        out, error = process.communicate(timeout=5)
        assert process.returncode == 0, error
        result = json.loads(out)
        result['receiver_host_preparation_resources'] = state
        return result
    finally:
        if process.poll() is None:
            process.communicate(timeout=90)


def prepare(args):
    root = create(args.root)
    state = begin_guard(args.which)
    digest = hashlib.sha256()
    with (root / 'source.bin').open('xb') as stream:
        for offset in range(0, args.size, 1024**2):
            block = hashlib.shake_256(b'cpnetflux-send32-' + offset.to_bytes(8, 'little')).digest(min(1024**2, args.size-offset))
            stream.write(block)
            digest.update(block)
            observe(args.which, [], state)
    assert sha(root / 'source.bin') == digest.hexdigest()
    observe(args.which, [], state)
    return {'source_sha256': digest.hexdigest(), 'bytes': args.size, 'resources': state}


def client(args):
    source = owned(args.root) / 'source.bin'
    state = begin_guard(2)
    started = time.monotonic()
    process = subprocess.Popen(command('client', 2, args.port, args.size, args.mode, source),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=clean_env())
    try:
        while process.poll() is None:
            observe(2, [process.pid], state)
            assert time.monotonic()-started < 45, 'client process deadline'
            time.sleep(.005)
        out, error = process.communicate(timeout=5)
        observe(2, [], state)
        assert process.returncode == 0, error
        result = json.loads(out)
        process_wall = time.monotonic()-started
        source_sha = sha(source)
        observe(2, [], state)
        row = {'result': result, 'resources': state, 'stdout': out, 'stderr': error,
               'source_sha256_after': source_sha, 'wall_spawn_to_exit': process_wall}
        save(args.evidence / (args.name + '-client.json'), row)
        return row
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)


def run(args, name, size, mode, expected):
    root = owned(args.root)
    target = root / 'target.bin'
    assert not target.exists()
    with socket.socket() as sock:
        sock.bind(('192.168.100.1', 0))
        port = sock.getsockname()[1]
    state = begin_guard(1)
    server = subprocess.Popen(command('server', 1, port, size, mode, target),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=clean_env())
    sender = None
    try:
        assert select.select([server.stdout], [], [], 10)[0], 'server readiness timeout'
        assert server.stdout.readline().strip() == 'CPNETFLUX_TCP_SEND_READY'
        sender = subprocess.Popen(peer(args, 'client', size=size, mode=mode, port=port, name=name),
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        started = time.monotonic()
        while server.poll() is None or sender.poll() is None:
            observe(1, [server.pid], state)
            assert time.monotonic()-started < 65, 'pair deadline'
            time.sleep(.005)
        out, error = server.communicate(timeout=5)
        remote_out, remote_error = sender.communicate(timeout=5)
        save(args.evidence / (name + '-raw.json'), {'server_stdout': out, 'server_stderr': error,
                                                  'remote_stdout': remote_out, 'remote_stderr': remote_error})
        assert server.returncode == 0, error
        assert sender.returncode == 0, remote_error
        sent = json.loads(remote_out)
        received = json.loads(out)
        actual = sha(target)
        observe(1, [], state)
        assert actual == expected == sent['source_sha256_after'], 'external full SHA mismatch'
        assert received['bytes'] == size == sent['result']['bytes']
        assert received['frames'] == sent['result']['frames']
        if mode == 'zerocopy':
            result = sent['result']
            assert result['successful_send_calls'] == result['copied_release_calls'] + result['unmarked_release_calls']
        row = {'name': name, 'mode': mode, 'size': size, 'port': port, 'sender': sent,
               'receiver': received, 'receiver_resources': state, 'target_sha256': actual}
        save(args.evidence / (name + '-result.json'), row)
        for resources in (state, sent['resources']):
            for key in resources['before']['net']:
                if key not in ('rx_bytes', 'tx_bytes'):
                    assert resources['after']['net'][key] == resources['before']['net'][key], ('NIC error/drop delta', key)
        return row
    finally:
        if server.poll() is None:
            server.kill()
        server.communicate(timeout=5)
        if sender is not None:
            # Remote helper has its own45s bound. A closed receiver wakes its client.
            sender.communicate(timeout=70)
        target.unlink(missing_ok=True)


def matrix(args):
    assert args.which == 1
    local, other = capability(args.commit, 1), remote(args, 'capability')
    assert local['receipt'] == other['receipt'], 'two-node source/config/binary mismatch'
    save(args.evidence / 'capability-r1.json', local)
    save(args.evidence / 'capability-r2.json', other)
    batches = []
    for label, size, pairs in [('probe', 64*1024**2, 1), ('matrix', 1024**3, 5)]:
        create(args.root)
        try:
            prepared = remote(args, 'prepare', size=size)
            save(args.evidence / (label + '-source.json'), prepared)
            rows = []
            for pair in range(pairs):
                order = ('copied', 'zerocopy') if pair % 2 == 0 else ('zerocopy', 'copied')
                for mode in order:
                    name = f'{label}-{pair+1:02d}-{mode}'
                    row = run(args, name, size, mode, prepared['source_sha256'])
                    rows.append(row)
                    print(json.dumps({'sample': name, 'sender_seconds': row['sender']['result']['service_seconds'],
                                      'sender_cpu': row['sender']['result']['cpu_seconds']}), flush=True)
            zc = [r['sender']['result'] for r in rows if r['mode'] == 'zerocopy']
            assert all(r['unmarked_release_calls'] > r['copied_release_calls'] for r in zc), 'mostly copied physical-NIC completion; stop'
            batches.extend(rows)
        finally:
            local_clean = cleanup(args.root)
            remote_clean = remote(args, 'cleanup')
            save(args.evidence / (label + '-cleanup.json'), {'r1': local_clean, 'r2': remote_clean})
    summary = {}
    for mode in ('copied', 'zerocopy'):
        rows = [r for r in batches if r['name'].startswith('matrix-') and r['mode'] == mode]
        values = [r['sender']['result']['service_seconds'] for r in rows]
        cpu = [r['sender']['result']['cpu_seconds'] for r in rows]
        summary[mode] = {'values_seconds': values, 'median_seconds': statistics.median(values),
                         'cv_population': statistics.pstdev(values)/statistics.mean(values),
                         'sender_cpu_median_seconds': statistics.median(cpu),
                         'receiver_service_seconds': [r['receiver']['service_seconds'] for r in rows]}
    gain = 1-summary['zerocopy']['median_seconds']/summary['copied']['median_seconds']
    summary['wall_reduction_fraction'] = gain
    summary['local_gate_pass'] = gain >= .15 and all(summary[m]['cv_population'] <= .05 for m in ('copied', 'zerocopy'))
    summary['complete_task_budget_established'] = False
    summary['runtime_integration'] = False
    summary['scope'] = 'standalone V3 send/fresh mapped sink; no complete checkpoint/publication benchmark'
    summary['final_snapshots'] = {'r1': snapshot(1), 'r2': remote(args, 'capability')['snapshot']}
    for key, before in (('r1', local['snapshot']), ('r2', other['snapshot'])):
        assert summary['final_snapshots'][key]['pswpout'] == before['pswpout'], 'overall host swap-out delta'
    save(args.evidence / 'summary.json', summary)
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action', required=True, choices=('capability', 'prepare', 'client', 'cleanup', 'matrix'))
    p.add_argument('--commit', required=True)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--which', type=int, choices=(1, 2), default=1)
    p.add_argument('--size', type=int, default=0)
    p.add_argument('--port', type=int, default=0)
    p.add_argument('--mode', choices=('copied', 'zerocopy'), default='copied')
    p.add_argument('--name', default='')
    args = p.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    assert args.evidence.parent == Path('/home/Su/experiments/CPNetFlux')
    assert args.evidence.name == '2026-11-10-v3-tcp-buffer-ownership-32'
    if args.action in ('prepare', 'client'):
        assert 4 <= args.size <= 1024**3
    if args.action == 'client':
        assert args.name and all(c.isalnum() or c == '-' for c in args.name)
    actions = {'capability': lambda: capability(args.commit, args.which), 'prepare': lambda: prepare(args),
               'client': lambda: client(args), 'cleanup': lambda: cleanup(args.root), 'matrix': lambda: matrix(args)}
    print(json.dumps(actions[args.action]()), flush=True)


if __name__ == '__main__':
    main()
