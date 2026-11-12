#!/usr/bin/env python3
"""Real V3 upload/download with a socket ceiling; no throughput claim."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_large_data_smoke import command

def receipts(text):
    prefix='CPNETFLUX_TCP_KERNEL_PACING '
    return [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]

def validate(rows):
    assert len(rows)==4 and {r['lane'] for r in rows}==set(range(4)),rows
    assert all(r['enabled'] is True and r['requested_bytes_per_second']==2000000000 and
               r['effective_ceiling_bytes_per_second']==2000000000 and
               r['application_pacer_enabled'] is False for r in rows),rows

def run(build):
    with tempfile.TemporaryDirectory(prefix='cpnetflux-kernel-pacing-') as temp:
        root=Path(temp);source=root/'source';peer=root/'peer';target=root/'target'
        for path in (source,peer,target):path.mkdir()
        block=bytes(range(251))*1045
        (source/'large.bin').write_bytes(block*65+block[:19])
        (source/'empty').touch()
        expected=tree_hash(source)
        port=free_port();log=root/'server.log'
        env={key:value for key,value in os.environ.items() if not key.startswith('CPNETFLUX_')}
        env.update(CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND='2000000000',
                   CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND='off')
        with log.open('w') as output:
            server=subprocess.Popen([str(build/'cpnetflux-gridftp-server'),'--host','127.0.0.1',
                '--port',str(port),'--root',str(peer),'--data-port-base',
                str(clamp_passive_data_port_base(free_port())),'--connections','4',
                '--link-set-max-links','4','--checksum','crc32c','--chunk-size','1048576'],
                stdout=output,stderr=subprocess.STDOUT,env=env)
        try:
            wait_for_control(port)
            results=[]
            for direction in ('upload','download'):
                summary=root/(direction+'.json')
                cmd=command(build,direction,port,source if direction=='upload' else 'received',
                            'received' if direction=='upload' else target,summary,262144)
                done=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=45)
                assert done.returncode==0,done.stdout+done.stderr+log.read_text()
                assert tree_hash(peer/'received' if direction=='upload' else target)==expected
                data=json.loads(summary.read_text())
                assert data['result']=='pass' and data['actual_transport']=='v3'
                assert data['bytes_transferred']==expected[2],data
                evidence=done.stderr if direction=='upload' else log.read_text()
                rows=receipts(evidence)
                validate(rows)
                assert 'CPNETFLUX_SENDER_PACING ' not in evidence
                results.append(dict(direction=direction,bytes=data['bytes_transferred'],receipts=rows))
            print(json.dumps(dict(result='pass',transfers=results,performance_claim=False)))
        finally:
            stop_server(server,log)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    run(parser.parse_args().build_dir.resolve())
