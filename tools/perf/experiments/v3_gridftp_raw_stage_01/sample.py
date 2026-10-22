import json
import os
import pathlib
import select
import subprocess
import sys
import time

out, interface = sys.argv[1:3]
with open(out, 'w') as f:
    while True:
        row = {'monotonic': time.monotonic(), 'epoch': time.time()}
        row['net'] = {p.name: int(p.read_text()) for p in (pathlib.Path('/sys/class/net') / interface / 'statistics').iterdir()}
        row['cpu'] = pathlib.Path('/proc/stat').read_text().splitlines()[0]
        row['diskstats'] = pathlib.Path('/proc/diskstats').read_text()
        row['meminfo'] = pathlib.Path('/proc/meminfo').read_text()
        row['processes'] = []
        for p in pathlib.Path('/proc').glob('[0-9]*'):
            try:
                comm = (p / 'comm').read_text().strip()
                if not (comm.startswith('cpnetflux-') or comm.startswith('globus-')): continue
                row['processes'].append({'pid':int(p.name), 'comm':comm,
                    'stat':(p/'stat').read_text(), 'io':(p/'io').read_text(),
                    'fd_count':len(list((p/'fd').iterdir()))})
            except (OSError, ValueError): pass
        row['sockets'] = subprocess.run(['ss','-tinmH','dst','192.168.100.0/30'],capture_output=True,text=True).stdout
        f.write(json.dumps(row)+'\n'); f.flush()
        if len(sys.argv)>3:
            if select.select([sys.stdin],[],[],.5)[0]: break
        else: time.sleep(.5)
