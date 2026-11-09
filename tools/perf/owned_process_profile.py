#!/usr/bin/env python3
"""Bounded owned-child CPU profiling; acknowledged enable before gated exec."""
import ctypes
import gzip
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import threading
import time

import run_small_file_baseline as base

SCRIPT=Path(__file__).resolve()
CAP=32*base.MiB
CATEGORIES={
    'summary':r'treeHashFromFiles|writeTreeJsonSummary|computeTreeVerificationHash',
    'checkpoint':r'saveDownloadManifest|saveTransferManifest|ManifestStore|serialize.*Manifest|saveTreeManifest|TreeManifestWriter',
    'metadata':r'openat|newfstatat|statx|vfs_stat|lookup_fast|link_path_walk|create_directories',
    'publish':r'rename|unlink',
    'network':r'tcp_|sock_|Socket::|recv|send|FramedSocket',
    'checksum':r'crc32c|Crc32c|computeChecksum',
    'allocation':r'malloc|calloc|realloc|operator new|_M_create|_M_mutate',
}


def validate_identity(pid,start,info):
    assert pid not in (126766,164834),'production PID forbidden'
    assert info['pid']==pid and info['start_ticks']==start,'owned PID identity changed'


def analyze_samples(samples):
    assert samples,'no CPU samples'
    total=sum(s['period'] for s in samples);assert total>0
    inclusive={};leaves={};threads={}
    for s in samples:
        period=s['period'];symbols=s['symbols'];assert symbols
        leaf=symbols[0];leaves[leaf]=leaves.get(leaf,0)+period
        tid=str(s['tid']);threads[tid]=threads.get(tid,0)+period
        joined='\n'.join(symbols)
        for key,pattern in CATEGORIES.items():
            if re.search(pattern,joined):inclusive[key]=inclusive.get(key,0)+period
    return {'scope':'sampled CPU periods, not wall; inclusive categories overlap; no off-CPU stacks',
        'samples':len(samples),'sampled_cpu_ns':total,
        'inclusive':{k:{'cpu_ns':v,'fraction':v/total} for k,v in inclusive.items()},
        'top_leaf_symbols':[{'symbol':k,'cpu_ns':v,'fraction':v/total}
                            for k,v in sorted(leaves.items(),key=lambda kv:-kv[1])[:40]],
        'threads':threads}


def parse_script(text):
    samples=[];current=None;lost=[]
    header=re.compile(r'^\s*(.*?)\s+(\d+)(?:/(\d+))?\s+(?:\[\d+\]\s+)?(\d+\.\d+):\s+(\d+)\s+cpu-clock:\s*(.*)$')
    frame=re.compile(r'^\s*[0-9a-f]+\s+(.+?)\s+\([^)]*\)\s*$')
    for line in text.splitlines():
        if 'LOST' in line.upper():lost.append(line)
        h=header.match(line)
        if h:
            current={'pid':int(h[2]),'tid':int(h[3] or h[2]),'at_ns':int(float(h[4])*1e9),
                     'period':int(h[5]),'symbols':[]}
            samples.append(current)
            f=frame.match(h[6])
            if f:current['symbols'].append(f[1])
        elif current:
            f=frame.match(line)
            if f:current['symbols'].append(f[1])
    assert samples and all(s['symbols'] for s in samples),'unrecognized perf script format'
    return samples,lost


def thread_snapshot(pid):
    rows=[]
    for directory in Path('/proc',str(pid),'task').iterdir():
        try:
            fields=(directory/'stat').read_text().rsplit(')',1)[1].split()
            status=dict(line.split(':',1) for line in (directory/'status').read_text().splitlines() if ':' in line)
            rows.append({'tid':int(directory.name),'start_ticks':int(fields[19]),'state':fields[0],
                'cpu_ticks':int(fields[11])+int(fields[12]),'wchan':(directory/'wchan').read_text().strip(),
                'voluntary':int(status['voluntary_ctxt_switches']),
                'involuntary':int(status['nonvoluntary_ctxt_switches'])})
        except (FileNotFoundError,ProcessLookupError):pass
    return {'at_ns':time.monotonic_ns(),'threads':rows}


class OwnedProcess:
    def __init__(self,argv,log,root,folder,sampled,env=None):
        base.check_root(root)
        assert any(str(a)==str(root) or str(a).startswith(str(root)+'/') for a in argv),'owned root missing from argv'
        self.root=root;self.folder=folder;self.sampled=sampled;self.perf=None;self.fds=[]
        self.closed=False;self.sampling_finished=False;self.release_ns=None;self.thread_rows=[];self.exit_info={}
        folder.mkdir(parents=True,exist_ok=False)
        for name in ('release','ctl','ack'):os.mkfifo(folder/name,0o600)
        self.release_fd=os.open(folder/'release',os.O_RDWR|os.O_NONBLOCK);self.fds.append(self.release_fd)
        self.log=log.open('x')
        self.process=subprocess.Popen([sys.executable,str(SCRIPT),'gate',str(folder),*map(str,argv)],
            stdout=self.log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
        def wait_for_exit():
            self.exit_info['code']=self.process.wait();self.exit_info['at_ns']=time.monotonic_ns()
        self.waiter=threading.Thread(target=wait_for_exit);self.waiter.start()
        try:
            deadline=time.monotonic()+10
            while not (folder/'ready.json').exists():
                assert self.process.poll() is None,'gated child exited before ready'
                assert time.monotonic()<deadline,'gate ready timeout'
                time.sleep(.005)
            ready=json.loads((folder/'ready.json').read_text())
            self.pid=self.process.pid;self.start_ticks=ready['start_ticks']
            assert ready['pid']==self.pid,'gate PID mismatch'
            validate_identity(self.pid,self.start_ticks,base.proc_info(self.pid))
            assert Path('/proc',str(self.pid)).stat().st_uid==os.getuid()
            if sampled:self._enable()
            base.save(folder/'collector-ready.json',{'pid':self.pid,'start_ticks':self.start_ticks,
                'sampled':sampled,'perf_ready':bool(self.perf),'command':argv,'clock':'CLOCK_MONOTONIC',
                'period_frequency_hz':199,'stack':'dwarf,4096','data_cap_bytes':CAP})
        except BaseException:
            self.close();raise

    def _command(self,text,ack=True):
        assert self.perf and self.perf.poll() is None,'profiler exited early'
        os.write(self.ctl,(text+'\n').encode())
        if ack:
            assert select.select([self.ack],[],[],10)[0],f'profiler ack timeout: {text}'
            assert b'ack' in os.read(self.ack,4096)

    def _enable(self):
        self.ctl=os.open(self.folder/'ctl',os.O_RDWR|os.O_NONBLOCK)
        self.ack=os.open(self.folder/'ack',os.O_RDWR|os.O_NONBLOCK);self.fds.extend([self.ctl,self.ack])
        argv=['sudo','-n','perf','record','-e','cpu-clock','-F','199','--call-graph','dwarf,4096',
            '--clockid','mono','--timestamp','--no-buildid','--delay=-1','--control',
            f'fifo:{self.folder}/ctl,{self.folder}/ack','-p',str(self.pid),'-o',str(self.folder/'perf.data')]
        with (self.folder/'perf.stderr').open('x') as f:
            self.perf=subprocess.Popen(argv,stdout=subprocess.DEVNULL,stderr=f,start_new_session=True)
        self._command('enable')

    def release(self):
        assert self.release_ns is None
        validate_identity(self.pid,self.start_ticks,base.proc_info(self.pid))
        self.release_ns=time.monotonic_ns();os.write(self.release_fd,b'1')

    def observe(self,state):
        base.observe(self.process,state)
        if self.sampled and not self.sampling_finished:
            data=self.folder/'perf.data'
            assert not data.exists() or data.stat().st_size<=CAP,'perf data cap'
            assert self.perf.poll() is None or self.process.poll() is not None,'profiler exited while target alive'
        if state['samples']%2==0 and self.process.poll() is None:
            try:self.thread_rows.append(thread_snapshot(self.pid))
            except (FileNotFoundError,ProcessLookupError):pass
        assert len(self.thread_rows)<=6500,'thread snapshot cap'

    def finish_sampling(self):
        if (self.folder/'profile.json').exists():return json.loads((self.folder/'profile.json').read_text())
        result={'sampled':self.sampled,'target_pid':self.pid,'start_ticks':self.start_ticks}
        if self.perf:
            if self.perf.poll() is None:
                if self.process.poll() is None:
                    validate_identity(self.pid,self.start_ticks,base.proc_info(self.pid))
                    self._command('disable');self._command('stop',ack=False)
                # perf stops automatically after an exiting target's inherited threads.
            rc=self.perf.wait(timeout=10);assert rc==0,(rc,(self.folder/'perf.stderr').read_text())
            data=self.folder/'perf.data';assert data.stat().st_size<=CAP
            script=subprocess.check_output(['sudo','-n','perf','script','--ns','--show-lost-events',
                '-i',str(data),'-F','comm,pid,tid,time,period,event,ip,sym,dso'],text=True,stderr=subprocess.PIPE,timeout=20)
            assert len(script.encode())<=CAP,'perf script cap'
            with gzip.open(self.folder/'perf.script.gz','wt') as f:f.write(script)
            samples,lost=parse_script(script);assert not lost,('lost samples',lost)
            assert all(s['pid']==self.pid for s in samples),'sample escaped owned PID'
            result.update(analyze_samples(samples));result.update(perf_exit=rc,data_bytes=data.stat().st_size,
                lost_events=lost,first_sample_ns=min(s['at_ns'] for s in samples),last_sample_ns=max(s['at_ns'] for s in samples))
        base.save(self.folder/'profile.json',result)
        base.save(self.folder/'thread-snapshots.json',self.thread_rows)
        self.sampling_finished=True
        return result

    def close(self):
        if self.closed:return
        try:
            if self.process.poll() is None:
                self.process.terminate()
                try:self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=5)
            self.waiter.join(timeout=5);assert not self.waiter.is_alive()
            if self.perf and self.perf.poll() is None:
                try:self.perf.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    subprocess.run(['sudo','-n','kill','-INT','--',f'-{self.perf.pid}'],check=True)
                    self.perf.wait(timeout=10)
        finally:
            self.log.close()
            for fd in self.fds:os.close(fd)
            for name in ('release','ctl','ack'):(self.folder/name).unlink(missing_ok=True)
            self.closed=True


def gate(folder,argv):
    base.save(folder/'ready.json',base.proc_info(os.getpid()))
    with (folder/'release').open('rb',buffering=0) as f:assert f.read(1)==b'1'
    os.execvpe(argv[0],argv,os.environ)


def smoke_work(root):
    tids=[]
    def worker():
        tids.append(threading.get_native_id());begin=time.thread_time()
        libc=ctypes.CDLL(None)
        while time.thread_time()-begin<.3:libc.clock_gettime(1,ctypes.byref((ctypes.c_long*2)()))
    workers=[threading.Thread(target=worker) for _ in range(2)]
    for worker in workers:worker.start()
    for worker in workers:worker.join()
    base.save(root/'worker-tids.json',tids)


def selftest(root,folder):
    assert not root.exists();root.mkdir();(root/base.MARKER).write_text(base.TOKEN)
    process=None
    try:
        process=OwnedProcess([sys.executable,str(SCRIPT),'smoke-work',str(root)],folder.with_suffix('.log'),root,folder,True)
        state=base.resource_state();process.release()
        while process.process.poll() is None:
            process.observe(state);assert time.monotonic_ns()-process.release_ns<10*1e9;time.sleep(.01)
        process.waiter.join();assert process.exit_info['code']==0
        profile=process.finish_sampling()
        tids=json.loads((root/'worker-tids.json').read_text())
        assert len(tids)==2 and all(str(t) in profile['threads'] for t in tids),profile
        assert any('[kernel.kallsyms]' in line for line in gzip.open(folder/'perf.script.gz','rt'))
        base.save(folder/'selftest.json',{'pass':True,'worker_tids':tids,'profile':profile})
        print(json.dumps({'pass':True,'worker_tids':tids,'samples':profile['samples']}))
    finally:
        if process:process.close()
        (root/'worker-tids.json').unlink(missing_ok=True);(root/base.MARKER).unlink();root.rmdir()


if __name__=='__main__':
    action,*args=sys.argv[1:]
    if action=='gate':gate(Path(args[0]),args[1:])
    elif action=='smoke-work':smoke_work(Path(args[0]))
    elif action=='selftest':selftest(Path(args[0]),Path(args[1]))
    else:raise ValueError(action)
