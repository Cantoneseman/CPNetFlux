#!/usr/bin/env python3
"""Sample one owned tmpfs experiment; stop its processes on memory pressure."""
import argparse
import json
import os
from pathlib import Path
import signal
import threading
import time

GIB = 1024 ** 3


def owned_arguments(arguments, project, payload):
    binaries = {str(project / 'build' / name) for name in
                ['cpnetflux-tree-download-client', 'cpnetflux-tree-upload-client', 'cpnetflux-gridftp-server']}
    return bool(arguments and arguments[0] in binaries and any(
        argument == str(payload) or argument.startswith(str(payload) + '/')
        for argument in arguments[1:]))


def violation(available, processes, minimum, maximum):
    if available < minimum:
        return 'host_available_below_minimum'
    if sum(process['rss_bytes'] for process in processes) > maximum:
        return 'owned_process_rss_above_maximum'
    return None


def memory():
    return {line.split(':')[0]: int(line.split()[1]) * 1024
            for line in Path('/proc/meminfo').read_text().splitlines() if line.endswith('kB')}


def swap_activity():
    return {parts[0]: int(parts[1]) for line in Path('/proc/vmstat').read_text().splitlines()
            if (parts := line.split())[0] in ['pswpin', 'pswpout']}


def processes(project, payload):
    result = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if entry.stat().st_uid != os.getuid():
                continue
            arguments = [item.decode() for item in (entry / 'cmdline').read_bytes().split(b'\0') if item]
            if not owned_arguments(arguments, project, payload):
                continue
            fields = (entry / 'stat').read_text().rsplit(') ', 1)[1].split()
            values = {line.split(':')[0]: int(line.split()[1]) * 1024
                      for line in (entry / 'status').read_text().splitlines() if line.endswith('kB')}
            io = {line.split(':')[0]: int(line.split()[1])
                  for line in (entry / 'io').read_text().splitlines()}
            result.append({'pid': int(entry.name), 'start_ticks': int(fields[19]),
                           'rss_bytes': values.get('VmRSS', 0), 'hwm_bytes': values.get('VmHWM', 0),
                           'locked_bytes': values.get('VmLck', 0), 'swap_bytes': values.get('VmSwap', 0),
                           'minor_faults': int(fields[7]), 'major_faults': int(fields[9]),
                           'user_ticks': int(fields[11]), 'system_ticks': int(fields[12]), 'io': io})
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return result


def terminate_owned(items, project, payload):
    stopped = []
    for process in items:
        try:
            descriptor = os.pidfd_open(process['pid'])
            try:
                current = next((row for row in processes(project, payload)
                                if row['pid'] == process['pid'] and
                                row['start_ticks'] == process['start_ticks']), None)
                if current:
                    signal.pidfd_send_signal(descriptor, signal.SIGTERM)
                    stopped.append(process['pid'])
            finally:
                os.close(descriptor)
        except ProcessLookupError:
            pass
    return stopped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--payload-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--minimum-available-gib', type=int, default=6)
    parser.add_argument('--maximum-process-rss-gib', type=int, default=4)
    args = parser.parse_args()
    project, payload = args.project_root.resolve(), args.payload_root
    if (not payload.is_absolute() or payload.parent != Path('/dev/shm') or
            not payload.name.startswith('cpnetflux-') or payload.is_symlink() or
            args.minimum_available_gib < 1 or args.maximum_process_rss_gib < 1):
        parser.error('requires an explicitly owned /dev/shm/cpnetflux-* root and positive budgets')
    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    output = args.output
    result = {'sample_count': 0, 'minimum_available_bytes': None,
              'maximum_owned_rss_bytes': 0, 'maximum_owned_hwm_bytes': 0,
              'maximum_owned_swap_bytes': 0, 'maximum_tmpfs_used_bytes': 0,
              'violations': [], 'processes': {}, 'swap_before': swap_activity(),
              'minimum_required_bytes': args.minimum_available_gib * GIB,
              'maximum_allowed_rss_bytes': args.maximum_process_rss_gib * GIB,
              'sampling_seconds': 0.05, 'scope': 'owned process RSS excludes tmpfs pages; host swap counters include unrelated processes'}
    with output.with_suffix('.jsonl').open('x') as log:
        while True:
            values = memory()
            items = processes(project, payload)
            stats = os.statvfs('/dev/shm')
            used = (stats.f_blocks - stats.f_bfree) * stats.f_frsize
            available = values['MemAvailable']
            reason = violation(available, items, result['minimum_required_bytes'],
                               result['maximum_allowed_rss_bytes'])
            row = {'monotonic_ns': time.monotonic_ns(), 'available_bytes': available,
                   'tmpfs_used_bytes': used, 'swap': swap_activity(), 'processes': items}
            log.write(json.dumps(row, separators=(',', ':')) + '\n')
            log.flush()
            result['sample_count'] += 1
            result['minimum_available_bytes'] = min(available, result['minimum_available_bytes'] or available)
            result['maximum_owned_rss_bytes'] = max(result['maximum_owned_rss_bytes'], sum(p['rss_bytes'] for p in items))
            result['maximum_owned_hwm_bytes'] = max(result['maximum_owned_hwm_bytes'], sum(p['hwm_bytes'] for p in items))
            result['maximum_owned_swap_bytes'] = max(result['maximum_owned_swap_bytes'], sum(p['swap_bytes'] for p in items))
            result['maximum_tmpfs_used_bytes'] = max(result['maximum_tmpfs_used_bytes'], used)
            for item in items:
                key = str(item['pid']) + ':' + str(item['start_ticks'])
                result['processes'][key] = item
            if reason:
                result['violations'].append({'reason': reason, 'monotonic_ns': row['monotonic_ns'],
                                             'terminated_pids': terminate_owned(items, project, payload)})
                break
            if stop.wait(0.05):
                break
    result['swap_after'] = swap_activity()
    result['swap_delta_pages'] = {key: result['swap_after'][key] - result['swap_before'][key]
                                  for key in result['swap_before']}
    result['pass'] = not result['violations']
    with output.with_suffix('.json').open('x') as receipt:
        json.dump(result, receipt, indent=2)
        receipt.write('\n')
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
