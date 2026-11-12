"""Read exact owned-child procfs entries with explicit terminal-race receipts."""
import errno
from pathlib import Path
import subprocess


class ProcessSampleEnded(Exception):
    """The owned child exited while its final procfs sample was being read."""


def read_owned_process_text(proc, entry, state):
    if entry not in ('stat', 'status', 'smaps'):
        raise ValueError('unsupported owned procfs entry')
    try:
        return (Path('/proc') / str(proc.pid) / entry).read_text()
    except OSError as error:
        if error.errno not in (errno.ENOENT, errno.ESRCH):
            raise
        try:
            # Only the exceptional teardown path waits; ordinary observations and
            # child-exit timing remain unchanged. A live/unconfirmed child fails.
            returncode = proc.wait(timeout=.05)
        except subprocess.TimeoutExpired:
            raise error
        state.setdefault('terminal_procfs_reads', []).append(dict(
            pid=proc.pid, entry=entry, errno=error.errno,
            confirmed_exit_code=returncode, resource_sample_added=False))
        raise ProcessSampleEnded(entry) from error
