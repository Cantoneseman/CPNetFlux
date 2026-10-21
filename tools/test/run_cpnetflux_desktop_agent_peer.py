#!/usr/bin/env python3
"""Exercise the real desktop RPC adapter against a separately started V3 peer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "desktop/tests"))
from test_contract import validate
from tree_smoke_common import tree_hash

class Peer:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(20)
        self.sock.connect(str(path))
        self.stream = self.sock.makefile("rb")
        self.count = 0
    def call(self, method, params):
        self.count += 1
        request = dict(jsonrpc="2.0", id=str(self.count), method=method, params=params)
        validate(request)
        self.sock.sendall((json.dumps(request)+"\n").encode())
        while True:
            response = json.loads(self.stream.readline())
            validate(response)
            if response.get("id") != request["id"]:
                continue
            if "error" in response:
                raise RuntimeError(method+": "+json.dumps(response["error"], ensure_ascii=False))
            return response["result"]
    def close(self):
        self.stream.close()
        self.sock.close()

def operation(): return str(uuid.uuid4())

def wait_terminal(peer, task):
    deadline = time.monotonic()+120
    while time.monotonic() < deadline:
        current = peer.call("transfer.get", dict(transfer_id=task["transfer_id"]))["snapshot"]
        if current["state"] in {"completed","failed","recoverable","cancelled"}:
            if current["state"] != "completed":
                raise RuntimeError("transfer did not complete: "+json.dumps(current,ensure_ascii=False))
            return current
        time.sleep(.1)
    raise RuntimeError("agent transfer deadline exceeded")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,required=True)
    parser.add_argument("--host",required=True)
    parser.add_argument("--port",type=int,required=True)
    parser.add_argument("--server-name",default="cpnetflux-agent-test")
    parser.add_argument("--ca-file",type=Path,required=True)
    parser.add_argument("--token-file",type=Path,required=True)
    parser.add_argument("--local-bind-address",required=True)
    parser.add_argument("--remote-path",required=True)
    parser.add_argument("--evidence-dir",type=Path,required=True)
    args=parser.parse_args()
    run=args.evidence_dir.absolute()
    run.mkdir(mode=0o700,parents=True,exist_ok=False)
    source=run/"source";source.mkdir()
    (source/"nested").mkdir()
    (source/"nested/compressible.bin").write_bytes(bytes(range(256))*8192)
    (source/"random.bin").write_bytes(os.urandom(2*1024*1024))
    (source/"empty").write_bytes(b"")
    (source/"small.txt").write_bytes(b"CPNetFlux desktop V3 integration\n")
    destination=run/"download";destination.mkdir()
    runtime=run/"runtime";state=run/"state"
    log=(run/"agent.log").open("w")
    process=subprocess.Popen([sys.executable,str(ROOT/"desktop/agent/cpnetflux_agent.py"),
        "--core-bin-dir",str(args.build_dir.absolute()),"--runtime-dir",str(runtime),
        "--state-dir",str(state)],stdout=log,stderr=subprocess.STDOUT)
    peer=None
    try:
        deadline=time.monotonic()+10
        while not (runtime/"agent.sock").exists():
            if process.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError("agent failed to start")
            time.sleep(.05)
        peer=Peer(runtime/"agent.sock")
        info=peer.call("agent.getInfo",{})
        contract=peer.call("agent.getContract",{})
        assert info["api_version"]=="3.0.0" and contract["runtime_protocol"]=="v3"
        assert next(x for x in contract["methods"] if x["method"]=="transfer.start")["support"]=="supported"
        profile=dict(node_id="peer",name="V3 acceptance peer",host=args.host,control_port=args.port,
            server_name=args.server_name,ca_path=str(args.ca_file.absolute()),auth_mode="token",
            credential_ref="file:"+str(args.token_file.absolute()),default_root_id="acceptance")
        peer.call("profile.create",dict(operation_id=operation(),profile=profile))
        probe=peer.call("node.testConnection",dict(node_id="peer",root_id="acceptance",path="",access="write"))
        assert probe["authentication"]=="authenticated" and probe["checks"]["tls_identity"]["state"]=="passed"
        assert probe["ready"] is False
        inventory=peer.call("node.network.inventory",dict(node_id="peer"))
        interface=next(x for x in inventory["interfaces"] if args.local_bind_address in x["addresses"])
        plan=peer.call("node.network.plan",dict(node_id="peer",inventory_revision=inventory["inventory_revision"],
            requested_links=1,peer_addresses=[],selections=[dict(interface_id=interface["interface_id"],local_address=args.local_bind_address)]))
        peer.call("node.network.apply",dict(operation_id=operation(),node_id="peer",plan_id=plan["plan_id"],
            inventory_revision=inventory["inventory_revision"],confirm=True))
        binding=peer.call("node.network.get",dict(node_id="peer"))
        assert binding["binding"]["plan_id"]==plan["plan_id"]
        verified=peer.call("node.network.verify",dict(node_id="peer",plan_id=plan["plan_id"]))
        assert verified["verified"] and not verified["physical_path_verified"]
        policy=dict(contract["policy_defaults"],channel_count=2,pending_window=4,
            control_pipeline_depth=2,reconnect_attempts=2,transfer_timeout_ms=45000,
            compression="auto",scheduler_mode="global",scheduler_policy="adaptive")
        records=[]
        expected_hash,files,total=tree_hash(source)
        for direction in ["upload","download"]:
            local=dict(kind="local",path=str(source if direction=="upload" else destination),node_id=None,root_id=None)
            remote=dict(kind="remote",path=args.remote_path,node_id="peer",root_id="acceptance")
            request=dict(direction=direction,source=local if direction=="upload" else remote,
                destination=remote if direction=="upload" else local,policy=policy)
            preview=peer.call("transfer.validate",request)
            assert preview["resolved_options"]["channel_count"]==2 and preview["resolved_options"]["queue_capacity"]==8
            task=peer.call("transfer.create",dict(request,operation_id=operation()))["snapshot"]
            if direction=="upload":
                # Queued tasks keep the reviewed endpoint and binding when profiles change.
                peer.call("profile.update",dict(operation_id=operation(),profile=dict(profile,host="192.0.2.99")))
                peer.call("node.network.clear",dict(operation_id=operation(),node_id="peer",expected_plan_id=plan["plan_id"],confirm=True))
            peer.call("transfer.start",dict(operation_id=operation(),transfer_id=task["transfer_id"],expected_revision=task["revision"]))
            final=wait_terminal(peer,task)
            evidence=peer.call("transfer.evidence",dict(transfer_id=task["transfer_id"],attempt_id=final["attempt"]["attempt_id"]))
            summary=evidence["summary"]
            assert evidence["observation"]=="final_summary"
            assert summary["actual_transport"]=="v3" and summary["link_set_accepted_links"]==2
            assert summary["control_tls_verified"] and summary["data_tls_verified"]
            assert summary["local_bind_addresses"]==[args.local_bind_address]
            assert final["metrics"]["bytes_total"]==total and final["metrics"]["bytes_committed"]==total
            page=peer.call("file.list",dict(transfer_id=task["transfer_id"],attempt_id=final["attempt"]["attempt_id"],cursor=None,limit=100))
            assert page["observation"]=="final_summary" and page["coverage"]=="observed_only"
            assert len(page["items"])==files
            nonempty=next(x for x in page["items"] if x["size"])
            ranges=peer.call("range.list",dict(transfer_id=task["transfer_id"],attempt_id=final["attempt"]["attempt_id"],
                file_id=nonempty["file_id"],generation=nonempty["generation"],cursor=None,limit=100))
            assert ranges["items"]
            records.append(dict(direction=direction,snapshot=final,evidence=evidence,file_page=page,range_page=ranges))
            peer.call("profile.update",dict(operation_id=operation(),profile=profile))
            if direction=="upload":
                peer.call("node.network.apply",dict(operation_id=operation(),node_id="peer",plan_id=plan["plan_id"],
                    inventory_revision=inventory["inventory_revision"],confirm=True))
        actual_hash,actual_files,actual_total=tree_hash(destination)
        assert (actual_hash,actual_files,actual_total)==(expected_hash,files,total)
        result=dict(result="pass",api_version=info["api_version"],files=files,bytes_total=total,
            tree_sha256=actual_hash,peer=args.host,probe=probe,network_verification=verified,transfers=records)
        (run/"result.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
        print(json.dumps(dict(result="pass",tree_sha256=actual_hash,files=files,bytes_total=total,
            directions=["upload","download"],evidence=str(run/"result.json"))))
    finally:
        if peer:peer.close()
        process.terminate()
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        log.close()

if __name__=="__main__":main()
