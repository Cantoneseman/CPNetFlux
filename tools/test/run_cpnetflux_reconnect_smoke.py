#!/usr/bin/env python3
"""Real client/server TCP faults: content, recovery budget, TLS, range and terminal loss."""
import argparse, contextlib, json, os, shutil, socket, subprocess, tempfile, time
from pathlib import Path
from cpnetflux_fault_proxy import FaultProxy
from tree_smoke_common import free_port, tree_hash, wait_for_control, stop_server

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--build-dir", type=Path, required=True)
    p.add_argument("--baseline", action="store_true")
    p.add_argument("--evidence-dir", type=Path)
    p.add_argument("--case", action="append", dest="selected_cases",
                   help="run only the named direction-policy-fault case")
    p.add_argument("--transfer-timeout-ms", type=int,
                   help="override the normal transfer deadline for bounded recovery checks")
    p.add_argument("--expect-lane-recovery", action="store_true",
                   help="require a data-only fault to recover on its existing lane")
    p.add_argument("--expect-global-recovery", action="store_true",
                   help="require a control disconnect to recover through a new batch")
    a=p.parse_args()
    if a.expect_lane_recovery and a.expect_global_recovery:
        p.error("choose one recovery expectation")
    if a.transfer_timeout_ms is not None and a.transfer_timeout_ms <= 0:
        p.error("--transfer-timeout-ms must be positive")
    context=contextlib.nullcontext(str(a.evidence_dir)) if a.evidence_dir else tempfile.TemporaryDirectory(prefix="cpnetflux-reconnect-")
    with context as tmp:
        root=Path(tmp); root.mkdir(exist_ok=True,parents=True)
        cert,key=root/"cert.pem",root/"key.pem"
        subprocess.run(["openssl","req","-x509","-newkey","rsa:2048","-nodes","-keyout",str(key),
            "-out",str(cert),"-days","1","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"],
            check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        cases=[("upload","fixed","reset"),("download","adaptive","reset")] if a.baseline else [
            ("upload","fixed","clean"),("download","adaptive","clean"),
            ("upload","fixed","reset"),("download","adaptive","reset"),
            ("upload","adaptive","tls-range"),("download","fixed","tls-range"),
            ("upload","fixed","tls-range"),("download","adaptive","tls-range"),
            ("upload","fixed","terminal"),("download","adaptive","terminal"),
            ("upload","fixed","result"),("download","adaptive","result"),
            ("upload","fixed","exhausted"),("download","adaptive","deadline"),
            ("upload","fixed","changed"),("upload","fixed","corrupt"),
            ("upload","fixed","tls-identity")]
        if a.selected_cases:
            known = {f"{direction}-{policy}-{kind}" for direction, policy, kind in cases}
            unknown = sorted(set(a.selected_cases) - known)
            if unknown:
                p.error("unknown --case value(s): " + ", ".join(unknown))
            selected = set(a.selected_cases)
            cases = [case for case in cases
                     if f"{case[0]}-{case[1]}-{case[2]}" in selected]
        records=[]
        for direction,policy,kind in cases:
            name=f"{direction}-{policy}-{kind}"; case=root/name; case.mkdir()
            source=case/"source"; source.mkdir()
            (source/"a.bin").write_bytes(b"first"*500)
            size=64*1024*1024 if kind=="tls-range" else 8*1024*1024
            (source/"b.bin").write_bytes(bytes(range(256))*(size//256))
            expected=tree_hash(source)
            remote=case/"remote";remote.mkdir()
            if direction=="download": shutil.copytree(source,remote/"source")
            port=free_port();base=45000
            tls=kind in ("tls-range","tls-identity")
            serverCmd=[str(a.build_dir/"cpnetflux-gridftp-server"),"--host","127.0.0.1",
                "--port",str(port),"--data-port-base",str(base),"--root",str(remote),
                "--connections","1","--checksum","crc32c","--chunk-size","65536"]
            if tls: serverCmd+=["--tls-mode","required","--tls-cert-file",str(cert),"--tls-key-file",str(key),"--data-tls-mode","required"]
            with (case/"server.log").open("w") as log:
                server=subprocess.Popen(serverCmd,stdout=log,stderr=subprocess.STDOUT)
            proxy=None
            try:
                if tls:
                    until=time.monotonic()+5
                    while True:
                        try:
                            with socket.create_connection(("127.0.0.1",port),timeout=0.2): break
                        except OSError:
                            if time.monotonic()>until: raise
                            time.sleep(0.02)
                else: wait_for_control(port)
                def fault_hook():
                    if kind=="changed":
                        item=source/"b.bin"; stamp=item.stat()
                        os.utime(item,(stamp.st_atime,stamp.st_mtime+10))
                    if kind=="corrupt":
                        item=remote/"received"/"a.bin"
                        assert item.exists(), "committed file must predate corrupt fault"
                        with item.open("r+b") as out: out.write(b"X")
                proxy=FaultProxy("127.0.0.2","127.0.0.1",port,base,
                    drop_bytes=0 if kind in ("clean","terminal","result","deadline","tls-identity") else 262144,
                    drops=20 if kind=="exhausted" else 1,
                    delay=0.01 if kind in ("deadline","changed") else 0.0002,
                    drop_terminal=kind=="terminal",drop_result=kind=="result",on_reset=fault_hook,log=case/"proxy.jsonl")
                target=remote/"received" if direction=="upload" else case/"downloaded"
                summary=case/"summary.json"
                cmd=[str(a.build_dir/f"cpnetflux-tree-{direction}-client"),"--host","127.0.0.2",
                    "--port",str(port),"--source-dir",str(source) if direction=="upload" else "source",
                    "--dest-dir","received" if direction=="upload" else str(target),
                    "--chunk-size","65536","--buffer-size","32768","--checksum","crc32c",
                    "--file-parallelism","1" if kind=="changed" else "2",
                    "--scheduler","global","--scheduler-policy",policy,
                    "--scheduler-metrics-dir",str(case/"metrics"),"--json-summary",str(summary)]
                if not a.baseline:
                    timeout_ms = 100 if kind == "deadline" else (a.transfer_timeout_ms or 30000)
                    cmd += ["--reconnect-attempts", "1" if kind == "exhausted" else "2",
                            "--transfer-timeout-ms", str(timeout_ms)]
                if tls: cmd+=["--tls-mode","required","--tls-ca-file",str(cert),
                    "--tls-server-name","wrong.invalid" if kind=="tls-identity" else "localhost","--data-tls-mode","required"]
                started=time.monotonic()
                result=subprocess.run(cmd,text=True,capture_output=True,timeout=40)
                (case/"client.log").write_text(result.stdout+result.stderr)
                data=json.loads(summary.read_text())
                negative=kind in ("exhausted","deadline","changed","corrupt","tls-identity")
                assert (result.returncode!=0)==negative, result.stdout+result.stderr
                if negative:
                    if kind=="exhausted":
                        assert data["reconnect_attempts"]==0, data
                        assert data["lane_local_recovery_count"]==1, data
                        assert data["recovery_attempts_used"]==1, data
                        assert "limit exceeded" in result.stderr, data
                    elif kind=="deadline":
                        assert time.monotonic()-started<3 and "deadline" in result.stderr, data
                    elif kind == "changed":
                        assert data["reconnect_attempts"]<=2, data
                        assert data["data_connect_count"]<=4, data
                    elif kind == "corrupt":
                        assert time.monotonic()-started < 3, data
                        assert "committed output checksum mismatch" in result.stderr, data
                        assert data["data_connect_count"] <= 2, data
                    else: assert data["reconnect_attempts"]==0, data
                else:
                    actual=tree_hash(target); assert actual==expected,(actual,expected)
                    reset=kind not in ("clean",)
                    lane_recovery = kind in ("reset", "tls-range", "result") and not a.baseline
                    global_recovery = kind == "terminal" and not a.baseline
                    if a.expect_lane_recovery:
                        assert lane_recovery, kind
                    if a.expect_global_recovery:
                        assert global_recovery, kind
                    if lane_recovery:
                        assert data["reconnect_attempts"] == 0, data
                        assert data["control_reconnect_count"] == 0, data
                        if kind == "result":
                            assert 1 <= data["lane_local_recovery_count"] <= 2, data
                            assert data["recovery_attempts_used"] == data["lane_local_recovery_count"], data
                        else:
                            assert data["lane_local_recovery_count"] == 1, data
                            assert data["recovery_attempts_used"] == 1, data
                    elif global_recovery:
                        assert data["reconnect_attempts"] == 1, data
                        assert data["lane_local_recovery_count"] == 0, data
                        assert data["recovery_attempts_used"] == 1, data
                        assert data["control_reconnect_count"] > 0, data
                    else:
                        assert data["reconnect_attempts"]==int(reset),data
                        assert data["connection_epoch"]==1+int(reset),data
                    if kind=="terminal":
                        # One outer retry may resend in-flight bytes, but must stay within
                        # the original attempt plus one full-tree retry.
                        assert expected[2] <= data["bytes_transferred"] <= 2*expected[2],data
                    if reset:
                        assert proxy.dropped==1,data
                        if kind in ("reset", "tls-range") and lane_recovery:
                            if direction == "upload":
                                # Buffered bytes can be replayed after a lane reset;
                                # bound the retry to at most one extra copy of the tree.
                                assert expected[2] <= data["bytes_transferred"] <= 2*expected[2],data
                            else:
                                assert data["bytes_transferred"] <= expected[2] + 4*65536,data
                        elif not lane_recovery:
                            assert data["resume_skipped_bytes"]>0,data
                            assert data["recovery_payload_bytes"] + data["resume_skipped_bytes"] == expected[2],data
                    if kind=="tls-range":
                        events=[json.loads(line) for line in (case/"metrics"/"scheduler_events.jsonl").read_text().splitlines()]
                        assert any(e.get("event")=="workitem_dispatch" and e.get("offset",0)>0 for e in events),events[:3]
                records.append({"case":name,"exit":result.returncode,"faults":proxy.dropped,
                    "seconds":time.monotonic()-started,"summary":data})
                print(name,"PASS",flush=True)
            finally:
                if proxy: proxy.close()
                stop_server(server,case/"server.log")
        (root/"matrix.json").write_text(json.dumps(records,indent=2)+"\n")
if __name__=="__main__": main()
