"""Exercise the compiled C++ client across a real Unix socket transport."""
import json
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

def run(binary,scenario):
    with tempfile.TemporaryDirectory() as tmp:
        path=str(Path(tmp)/"agent.sock")
        server=socket.socket(socket.AF_UNIX);server.bind(path);server.listen();server.settimeout(4)
        seen=[];failures=[];created=False;operation_ids=[]
        def serve():
            nonlocal created
            try:
                for session in range(2 if scenario in {"reconnect","uncertain"} else 1):
                    conn,_=server.accept()
                    with conn,conn.makefile("rb") as stream:
                        for line in stream:
                            req=json.loads(line);method=req["method"];p=req["params"];seen.append((method,p))
                            epoch="epoch-b" if session else "epoch-a"
                            if method=="agent.getInfo":
                                r=dict(kind="agent_info",api_version="3.0.0",adapter="real",agent_epoch=epoch)
                            elif method=="agent.getContract":
                                r=dict(kind="agent_contract",api_version="3.0.0",runtime_protocol="v2" if scenario=="bad_contract" else "v3",adapter="real",methods=[dict(method=m,support="supported") for m in ["transfer.validate","transfer.create","transfer.start"]],policy_defaults=dict(requested_mode="v3"))
                            elif method=="profile.list":r=dict(kind="profile_list",profiles=[dict(node_id="peer",default_root_id="root")] if scenario=="uncertain" else [])
                            elif method=="transfer.subscribe":
                                reset=p.get("agent_epoch")!=epoch or p.get("after_sequence") is None
                                snapshots=[dict(transfer_id="persisted",revision=1,state="queued")]
                                if scenario=="uncertain":snapshots=[dict(transfer_id="persisted",revision=1,state="queued")] if created else []
                                if session and scenario=="reconnect":snapshots.append(dict(transfer_id="other",revision=2,state="completed"))
                                r=dict(kind="subscription",agent_epoch=epoch,sequence=0,reset=reset,snapshots=snapshots if reset else [])
                            elif method=="transfer.validate":r=dict(kind="transfer_validation",valid=True)
                            elif method=="transfer.create":
                                operation_ids.append(p["operation_id"])
                                if not created:
                                    created=True
                                    break
                                r=dict(kind="transfer",snapshot=dict(transfer_id="persisted",revision=1,state="queued"))
                            elif method=="transfer.start":r=dict(kind="operation",snapshot=dict(transfer_id="persisted",revision=2,state="completed"))
                            else:raise AssertionError(method)
                            conn.sendall((json.dumps(dict(jsonrpc="2.0",id=req["id"],result=r))+"\n").encode())
                            if scenario=="reconnect" and session==0 and method=="transfer.subscribe":break
            except Exception as e:failures.append(repr(e))
            finally:server.close()
        t=threading.Thread(target=serve);t.start()
        out=subprocess.run([binary,path]+(["uncertain"] if scenario=="uncertain" else []),capture_output=True,text=True,timeout=5)
        t.join(3);assert not t.is_alive() and not failures,failures
        assert out.returncode==0,out.stderr
        result=json.loads(out.stdout)
        if scenario=="bad_contract":
            assert not result["ready"],result
            assert not any(m=="transfer.subscribe" for m,_ in seen),seen
        elif scenario=="uncertain":
            assert result["count"]==1 and not result["uncertain"] and result["step"]==3,(result,operation_ids,seen,out.stderr)
            assert len(operation_ids)==2 and operation_ids[0]==operation_ids[1],operation_ids
        else:
            assert result["count"]==(2 if scenario=="reconnect" else 1) and not result["stale"],result
            first=next(p for m,p in seen if m=="transfer.subscribe")
            assert first.get("after_sequence") is None,first
        print(scenario,result)
for scenario in ["reset","bad_contract","reconnect","uncertain"]:run(sys.argv[1],scenario)
