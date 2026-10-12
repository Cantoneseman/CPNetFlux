#!/usr/bin/env python3
"""Exercise relocated installed commands, private agent API and disabled service."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

parser=argparse.ArgumentParser()
parser.add_argument("--prefix",type=Path,required=True)
parser.add_argument("--fixtures",type=Path,required=True)
args=parser.parse_args()
prefix=args.prefix.resolve()
for name in ["cpnetflux-gridftp-server","cpnetflux-file-client","cpnetflux-file-download-client",
             "cpnetflux-tree-upload-client","cpnetflux-tree-download-client","cpnetflux-agent","cpnetflux-desktop"]:
    env=dict(os.environ,QT_QPA_PLATFORM="offscreen")
    r=subprocess.run([str(prefix/"bin"/name),"--help"],env=env,capture_output=True,timeout=10)
    assert r.returncode in {0,2} and b"Usage:" in r.stdout+r.stderr or name in {"cpnetflux-agent","cpnetflux-desktop"} and r.returncode==0,(name,r.stderr)
service=subprocess.run([str(prefix/"bin/cpnetflux-service"),"--config",str(prefix/"share/cpnetflux/deployment/server.example.json")],capture_output=True,timeout=5)
assert service.returncode!=0 and b"refused" in service.stderr
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    agent=subprocess.Popen([str(prefix/"bin/cpnetflux-agent"),"--runtime-dir",str(root/"runtime"),"--state-dir",str(root/"state")],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        path=root/"runtime/agent.sock"; deadline=time.monotonic()+5
        while not path.exists() and time.monotonic()<deadline and agent.poll() is None: time.sleep(.02)
        assert path.exists(),"installed agent failed"
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(3);sock.connect(str(path))
            with sock.makefile("rb") as stream:
                def rpc(method,params):
                    sock.sendall((json.dumps(dict(jsonrpc="2.0",id="test",method=method,params=params))+"\n").encode())
                    return json.loads(stream.readline())
                info=rpc("agent.getInfo",{})["result"]
                assert info["api_version"]=="2.0.0" and info["adapter"]=="real"
                fixture=json.loads(args.fixtures.read_text())
                create=next(x["message"]["params"] for x in fixture["valid"] if x["name"]=="transfer.create")
                error=rpc("transfer.create",create)["error"]
                assert error["data"]["code"]=="unsupported","real create must fail closed"
        assert path.stat().st_mode & 0o777 == 0o600
    finally:
        agent.terminate(); agent.communicate(timeout=5)
    assert not path.exists()
print("relocated installed commands, private API2 agent and fail-closed service/adapter passed")
