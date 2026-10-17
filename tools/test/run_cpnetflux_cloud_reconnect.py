#!/usr/bin/env python3
"""Run one owned cloud endpoint. SSH deploys this runner; CPNetFlux moves all payload."""
import argparse, hashlib, json, os, signal, socket, subprocess, time
from pathlib import Path
from cpnetflux_fault_proxy import FaultProxy

def tree_digest(root):
    files=[]
    for p in sorted(root.rglob("*")):
        if not p.is_file() or ".cpnetflux." in str(p.relative_to(root)) or ".part." in p.name: continue
        h=hashlib.sha256()
        with p.open("rb") as f:
            for block in iter(lambda:f.read(1048576),b""):h.update(block)
        files.append({"path":p.relative_to(root).as_posix(),"size":p.stat().st_size,"sha256":h.hexdigest()})
    return files

def make_source(root):
    root.mkdir(parents=True,exist_ok=False)
    (root/"a.bin").write_bytes(b"CPNetFlux cloud recovery\n"*128)
    with (root/"b.bin").open("wb") as f:
        block=bytes(range(256))*4096
        for _ in range(64):f.write(block)
    for p in root.iterdir():os.utime(p,(1700000000,1700000000))
    return tree_digest(root)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--mode",choices=["serve","client","inspect"],required=True)
    p.add_argument("--root",type=Path,required=True)
    p.add_argument("--deploy",type=Path,required=True)
    p.add_argument("--listen-host")
    p.add_argument("--peer")
    p.add_argument("--direction",choices=["upload","download"],default="upload")
    p.add_argument("--policy",choices=["fixed","adaptive"],default="fixed")
    p.add_argument("--fault",type=int,default=0)
    p.add_argument("--port",type=int,default=22410)
    p.add_argument("--data-base",type=int,default=23380)
    a=p.parse_args();a.root.mkdir(parents=True,exist_ok=True)
    if a.mode=="serve":
        remote=a.root/"remote";remote.mkdir(exist_ok=False)
        expected=make_source(remote/"source")
        cmd=[str(a.deploy/"cpnetflux-gridftp-server"),"--host","127.0.0.1","--port",str(a.port),
            "--root",str(remote),"--data-port-base",str(a.data_base),"--connections","1",
            "--checksum","crc32c","--chunk-size","65536","--auth-mode","token",
            "--auth-token-file",str(a.deploy/"token"),"--tls-mode","required",
            "--tls-cert-file",str(a.deploy/"cert.pem"),"--tls-key-file",str(a.deploy/"key.pem"),
            "--data-tls-mode","required"]
        proxy=None
        with (a.root/"server.log").open("w") as log:
            server=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
        (a.root/"pids.json").write_text(json.dumps({"runner":os.getpid(),"server":server.pid})+"\n")
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1",a.port),timeout=0.1):break
                except OSError:time.sleep(0.02)
            else:raise RuntimeError("owned server did not start")
            proxy=FaultProxy(a.listen_host,"127.0.0.1",a.port,a.data_base,
                drop_bytes=1048576 if a.fault else 0,log=a.root/"faults.jsonl",delay=0.0001)
            (a.root/"ready").write_text("ready\n")
            print("cloud endpoint READY",flush=True)
            end=time.monotonic()+180
            while not (a.root/"stop").exists() and time.monotonic()<end:
                if server.poll() is not None:raise RuntimeError("owned server exited")
                time.sleep(0.1)
            actual=tree_digest(remote/("received" if a.direction=="upload" else "source"))
            manifests=list((remote/"received").rglob("*.cpnetflux.manifest")) if a.direction=="upload" else []
            report={"expected":expected,"actual":actual,"hash_match":actual==expected,
                "faults":proxy.dropped,"committed_manifests":sum("state=committed\n" in f.read_text() for f in manifests)}
            (a.root/"server-result.json").write_text(json.dumps(report,indent=2)+"\n")
            if actual!=expected:raise RuntimeError("cloud receiver SHA-256 differs")
        finally:
            if proxy:proxy.close()
            server.terminate()
            try:server.wait(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.wait(timeout=5)
            (a.root/"stopped").write_text("owned server and proxy stopped\n")
    elif a.mode=="client":
        source=a.root/"source";expected=make_source(source)
        dest=a.root/"downloaded"
        cmd=[str(a.deploy/f"cpnetflux-tree-{a.direction}-client"),"--host",a.peer,
            "--port",str(a.port),"--source-dir",str(source) if a.direction=="upload" else "source",
            "--dest-dir","received" if a.direction=="upload" else str(dest),
            "--file-parallelism","2","--chunk-size","65536","--buffer-size","32768",
            "--checksum","crc32c","--scheduler","global","--scheduler-policy",a.policy,
            "--scheduler-metrics-dir",str(a.root/"metrics"),"--reconnect-attempts","2",
            "--transfer-timeout-ms","120000","--json-summary",str(a.root/"summary.json"),
            "--phase-timing","on","--auth-mode","token","--auth-token-file",str(a.deploy/"token"),
            "--tls-mode","required","--tls-ca-file",str(a.deploy/"cert.pem"),
            "--tls-server-name","cpnetflux-cloud-test","--data-tls-mode","required"]
        started=time.monotonic()
        with (a.root/"client.log").open("w") as log:
            result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=130)
        summary=json.loads((a.root/"summary.json").read_text())
        report={"exit":result.returncode,"seconds":time.monotonic()-started,"summary":summary,
            "expected":expected}
        if a.direction=="download":
            report["actual"]=tree_digest(dest)
            report["hash_match"]=report["actual"]==expected
            assert report["hash_match"],report
        (a.root/"client-result.json").write_text(json.dumps(report,indent=2)+"\n")
        assert result.returncode==0,(a.root/"client.log").read_text()
        assert summary["reconnect_attempts"]==a.fault,summary
        assert summary["connection_epoch"]==1+a.fault,summary
        assert summary["resume_skipped_bytes"]>0 if a.fault else summary["resume_skipped_bytes"]==0
        if a.fault:
            assert summary["recovery_payload_bytes"]+summary["resume_skipped_bytes"] == sum(f["size"] for f in expected),summary
        print("cloud client PASS",a.direction,a.policy,"reconnects",summary["reconnect_attempts"],
            "payload",summary["bytes_transferred"],"skipped",summary["resume_skipped_bytes"],flush=True)
    else:
        for p in [a.root/"client-result.json",a.root/"server-result.json"]:
            if p.exists():print(p.read_text())
if __name__=="__main__":main()
