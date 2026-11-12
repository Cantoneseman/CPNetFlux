"""Real TLS/token V3 transfers initiated by the actual QML controls.

Run explicitly with --core-build; never use the demo adapter for this gate.
Credentials, payloads, screenshots and state remain outside the repository.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools/test"))
from run_gridftp_control_mtls_smoke import make_credentials, free_port
from run_cpnetflux_desktop_agent_peer import Peer
from tree_smoke_common import tree_hash

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--ui-build",type=Path,required=True)
    parser.add_argument("--core-build",type=Path,required=True)
    parser.add_argument("--evidence-dir",type=Path,required=True)
    args=parser.parse_args()
    run=args.evidence_dir.resolve();run.mkdir(mode=0o700,parents=True,exist_ok=False)
    tls=run/"tls";tls.mkdir(mode=0o700)
    ca,cert,key,*_=make_credentials(tls)
    token=tls/"token";token.write_text(uuid.uuid4().hex);token.chmod(0o600)
    wrong=tls/"wrong";wrong.write_text(uuid.uuid4().hex);wrong.chmod(0o600)
    server_root=run/"server";server_root.mkdir()
    source=run/"source";source.mkdir()
    (source/"nested").mkdir();(source/"nested/small.txt").write_text("CPNetFlux V3 QML transfer\n")
    (source/"empty").touch();(source/"random.bin").write_bytes(os.urandom(40*1024*1024))
    for i in range(66):(source/"nested"/("item-%02d.txt"%i)).write_text("small file %d\n"%i)
    download=run/"download";download.mkdir()
    port=free_port();log=(run/"server.log").open("w")
    server=subprocess.Popen([str(args.core_build/"cpnetflux-gridftp-server"),"--host","127.0.0.1","--port",str(port),
        "--root",str(server_root),"--data-port-base","48000","--connections","2","--link-set-max-links","2","--checksum","crc32c",
        "--auth-mode","token","--auth-token-file",str(token),"--tls-mode","required",
        "--tls-cert-file",str(cert),"--tls-key-file",str(key),"--data-tls-mode","required"],stdout=log,stderr=subprocess.STDOUT)
    runtime=run/"runtime";state=run/"state"
    agent_log=(run/"agent.log").open("w")
    agent=subprocess.Popen([sys.executable,str(ROOT/"desktop/agent/cpnetflux_agent.py"),
        "--core-bin-dir",str(args.core_build),"--runtime-dir",str(runtime),"--state-dir",str(state)],
        stdout=agent_log,stderr=subprocess.STDOUT)
    peer=None
    try:
        deadline=time.monotonic()+8
        while not (runtime/"agent.sock").exists():
            assert time.monotonic()<deadline and agent.poll() is None,"agent startup"
            time.sleep(.02)
        peer=Peer(runtime/"agent.sock")
        profile=dict(node_id="loopback",name="V3 loopback",host="127.0.0.1",control_port=port,
            server_name="localhost",ca_path=str(ca),auth_mode="token",credential_ref="file:"+str(token),default_root_id="test")
        peer.call("profile.create",dict(operation_id=str(uuid.uuid4()),profile=profile))
        # The test probe waits for the server, and verifies the real TLS/auth path.
        deadline=time.monotonic()+8
        while True:
            probe=peer.call("node.testConnection",dict(node_id="loopback",root_id="test",path="",access="write"))
            if probe["authentication"]=="authenticated":break
            assert time.monotonic()<deadline,probe
            time.sleep(.1)
        assert probe["checks"]["tls_identity"]["state"]=="passed" and probe["ready"] is False
        env=dict(os.environ,QT_QPA_PLATFORM="offscreen",QT_QUICK_BACKEND="software",QT_QUICK_CONTROLS_STYLE="Fusion")
        tested_policy=dict(channel_count=2,pending_window=4,control_pipeline_depth=2,reconnect_attempts=2,
            transfer_timeout_ms=45000,compression="auto",scheduler_mode="global",scheduler_policy="adaptive")
        def drive(name,direction,local,remote,**extras):
            config=run/(name+".json");selected_policy=extras.pop("policy",tested_policy)
            config.write_text(json.dumps(dict(direction=direction,local=str(local),remote=remote,policy=selected_policy,**extras)))
            proc=subprocess.run([str(args.ui_build/"cpnetflux-ui-flow-test"),str(runtime/"agent.sock"),str(config)],
                capture_output=True,text=True,env=env,timeout=50)
            (run/(name+".stderr")).write_text(proc.stderr)
            assert proc.returncode==0,(name,proc.returncode,proc.stdout,proc.stderr)
            result=json.loads(proc.stdout);(run/(name+"-result.json")).write_text(json.dumps(result,indent=2))
            task=next(t for t in result["tasks"] if t["destination"]["path"]==(remote if direction=="upload" else str(local)))
            assert task["state"]=="completed" and task["actual_mode"]=="v3" and task["adapter"]=="real"
            assert task["commit_confirmed"] and task["integrity_status"]=="pass"
            assert task["backend_evidence"]["checksum"]["status"]=="pass"
            assert task["backend_evidence"]["manifest"]["status"]=="observed"
            assert task["backend_evidence"]["finalize"]["status"]=="pass"
            assert result["files"] and result["attempts"]
            assert not any(marker in proc.stderr for marker in ["TypeError","ReferenceError","Binding loop","Cannot assign"])
            return result,task
        upload,ut=drive("upload","upload",source,"qml-tree")
        assert upload["ranges"] and any(f["generation"]>1 and f["range_count"]>0 for f in upload["files"])
        assert tree_hash(source)==tree_hash(server_root/"qml-tree")
        down,dt=drive("download","download",download,"qml-tree")
        assert down["ranges"]
        assert tree_hash(source)==tree_hash(download)
        # A directory containing a single file still uses the authenticated V3 tree API.
        one=run/"one";one.mkdir();(one/"file.txt").write_text("one actual file\n")
        _,single=drive("one-file-tree","upload",one,"one-tree")
        assert tree_hash(one)==tree_hash(server_root/"one-tree")
        # UI exit/reopen recovers persistent task snapshots; the agent keeps running.
        reopened=subprocess.run([str(args.ui_build/"cpnetflux-desktop"),"--socket",str(runtime/"agent.sock"),
            "--smoke-ms","600","--screenshot",str(run/"tasks.png")],capture_output=True,text=True,env=env,timeout=8)
        assert reopened.returncode==0 and "snapshots=3" in reopened.stderr,reopened.stderr
        assert agent.poll() is None
        # Wrong token and wrong TLS identity are real failure paths, not simulated tasks.
        errors={}
        for name,patch in [("token",dict(credential_ref="file:"+str(wrong))),("tls",dict(server_name="wrong.invalid"))]:
            peer.call("profile.update",dict(operation_id=str(uuid.uuid4()),profile=dict(profile,**patch)))
            check=peer.call("node.testConnection",dict(node_id="loopback",root_id="test",path="",access="write"))
            assert check["authentication"]!="authenticated",check
            errors[name]=check
        peer.call("profile.update",dict(operation_id=str(uuid.uuid4()),profile=profile))
        # Cancel only after a real per-file checkpoint has persisted verified chunks.
        big=run/"cancel-source";big.mkdir()
        with (big/"payload.bin").open("wb") as f:
            block=os.urandom(1024*1024)
            for _ in range(128):f.write(block)
        detach_config=run/"detach.json";detach_config.write_text(json.dumps(dict(direction="upload",local=str(big),remote="detached-tree",detach=True)))
        detached=subprocess.run([str(args.ui_build/"cpnetflux-ui-flow-test"),str(runtime/"agent.sock"),str(detach_config)],
            capture_output=True,text=True,env=env,timeout=50)
        assert detached.returncode==0,detached.stdout+detached.stderr
        detached_result=json.loads(detached.stdout);assert detached_result["detached_while_active"]
        transfer_id=detached_result["transfer_id"]
        reopened_active=subprocess.run([str(args.ui_build/"cpnetflux-desktop"),"--socket",str(runtime/"agent.sock"),
            "--smoke-ms","900","--screenshot",str(run/"active-reopened.png")],capture_output=True,text=True,env=env,timeout=8)
        assert reopened_active.returncode==0 and "snapshots=4" in reopened_active.stderr,reopened_active.stderr
        deadline=time.monotonic()+15
        while True:
            current=peer.call("transfer.get",dict(transfer_id=transfer_id))["snapshot"]
            if current["state"]=="completed":break
            assert current["state"] not in {"failed","cancelled","recoverable"} and time.monotonic()<deadline,current
            time.sleep(.02)
        assert tree_hash(big)==tree_hash(server_root/"detached-tree")
        resumed,rt=drive("cancel-resume","upload",big,"cancel-tree",cancel=True,
            checkpoint=str(server_root/"cancel-tree/payload.bin.cpnetflux.manifest"),
            policy=dict(tested_policy,scheduler_work_item_min_bytes=268435456,scheduler_work_item_max_bytes=268435456))
        assert resumed["cancelled"] and resumed["resumed"] and rt["attempt"]["attempt_number"]==2
        assert tree_hash(big)==tree_hash(server_root/"cancel-tree")
        assert resumed["evidence"]["summary"]["resume"] is True
        queued_request=dict(operation_id=str(uuid.uuid4()),direction="upload",
            source=dict(kind="local",path=str(one),node_id=None,root_id=None),
            destination=dict(kind="remote",path="queued-tree",node_id="loopback",root_id="test"),
            policy=peer.call("agent.getContract",{})["policy_defaults"])
        queued=peer.call("transfer.create",queued_request)["snapshot"]
        assert queued["state"]=="queued"
        drive("queued-start","upload",one,"queued-tree",queued_task_id=queued["transfer_id"])
        # Absolute or escaping remote paths fail before core launch.
        invalid=dict(direction="upload",source=dict(kind="local",path=str(source),node_id=None,root_id=None),
            destination=dict(kind="remote",path="../escape",node_id="loopback",root_id="test"),policy=peer.call("agent.getContract",{})["policy_defaults"])
        raw=socket.socket(socket.AF_UNIX);raw.connect(str(runtime/"agent.sock"))
        with raw,raw.makefile("rb") as stream:
            raw.sendall((json.dumps(dict(jsonrpc="2.0",id="invalid-path",method="transfer.validate",params=invalid))+"\n").encode())
            rejected=json.loads(stream.readline())
        assert rejected["error"]["data"]["code"]=="protocol_error",rejected
        invalid["destination"]["path"]="single-file"
        invalid["source"]["path"]=str(source/"random.bin")
        try:peer.call("transfer.validate",invalid)
        except RuntimeError as e:assert "unsupported" in str(e)
        else:raise AssertionError("standalone authenticated file accepted")
        summary=dict(adapter="real",api="3.0.0",runtime="v3",upload=ut["transfer_id"],download=dt["transfer_id"],
            one_file_tree=single["transfer_id"],cancel_resume=rt["transfer_id"],hashes_match=True,
            authentication_errors=errors,ui_reopen=True)
        for page,width,height,scale in [(0,1180,780,"1"),(1,760,560,"1.25"),(2,900,650,"1"),(3,760,560,"1.5"),(6,900,650,"1")]:
            shot=run/("page-%d-%d-scale-%s.png"%(page,width,scale))
            rendered=subprocess.run([str(args.ui_build/"cpnetflux-desktop"),"--socket",str(runtime/"agent.sock"),
                "--page",str(page),"--window-width",str(width),"--window-height",str(height),"--smoke-ms","600",
                "--screenshot",str(shot)],capture_output=True,text=True,env=dict(env,QT_SCALE_FACTOR=scale),timeout=8)
            assert rendered.returncode==0,rendered.stderr
            assert not any(m in rendered.stderr for m in ["TypeError","ReferenceError","Cannot assign","Binding loop"]),rendered.stderr
        (run/"result.json").write_text(json.dumps(summary,indent=2))
        print(json.dumps(dict(evidence=str(run),scenarios=["qml-upload","qml-download","one-file-tree","ui-reopen","tls-token-errors","cancel-resume","queued-start","path-rejection"])))
    finally:
        if peer:peer.close()
        for p in [agent,server]:
            p.terminate()
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:p.kill();p.wait(timeout=5)
        log.close();agent_log.close()
if __name__=="__main__":main()
