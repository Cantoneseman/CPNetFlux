"""Audit an integrated DEB and start its extracted agent/UI without installing it."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

def call(path,method):
    sock=socket.socket(socket.AF_UNIX);sock.connect(str(path))
    with sock,sock.makefile("rb") as stream:
        sock.sendall((json.dumps(dict(jsonrpc="2.0",id="package",method=method,params={}))+"\n").encode())
        result=json.loads(stream.readline())
        assert "result" in result,result
        return result["result"]

def main():
    p=argparse.ArgumentParser()
    p.add_argument("deb",type=Path)
    args=p.parse_args()
    dependencies=subprocess.check_output(["dpkg-deb","-f",str(args.deb),"Depends"],text=True)
    for package in ["qt6-qpa-plugins","qml6-module-qtquick-templates","qml6-module-qtquick-dialogs"]:
        assert package in dependencies,dependencies
    with tempfile.TemporaryDirectory(prefix="cpnetflux-package-audit-") as tmp:
        root=Path(tmp);image=root/"image"
        subprocess.run(["dpkg-deb","-x",str(args.deb),str(image)],check=True)
        files=[f.relative_to(image).as_posix() for f in image.rglob("*") if f.is_file()]
        assert "usr/lib/systemd/user/cpnetflux-agent.service" in files
        for f in files:
            assert not any(fragment in f for fragment in ["/tls/","/state/","/build/","__pycache__","/tests/","ui-flow-test","rpc-test"]),f
            assert not f.endswith((".key",".pem",".cpnetflux",".events.jsonl",".manifest")),f
        bin_dir=image/"usr/bin"
        for name in ["cpnetflux-agent","cpnetflux-desktop","cpnetflux-tree-upload-client","cpnetflux-tree-download-client","cpnetflux-auth-probe"]:
            assert (bin_dir/name).is_file(),name
        env=dict(os.environ,QT_QPA_PLATFORM="offscreen",QT_QUICK_BACKEND="software",QT_QUICK_CONTROLS_STYLE="Fusion")
        env.pop("CPNETFLUX_CORE_BIN_DIR",None)
        agent=subprocess.Popen([str(bin_dir/"cpnetflux-agent"),"--runtime-dir",str(root/"runtime"),
            "--state-dir",str(root/"state")],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            path=root/"runtime/agent.sock";deadline=time.monotonic()+8
            while not path.exists():
                assert agent.poll() is None and time.monotonic()<deadline,"installed agent failed startup"
                time.sleep(.02)
            info=call(path,"agent.getInfo");contract=call(path,"agent.getContract")
            assert info["adapter"]=="real" and info["api_version"]=="3.0.0"
            assert "transfer_lifecycle" in info["features"],info
            assert all(row["support"]=="supported" for row in contract["methods"] if row["method"] in {"transfer.create","transfer.start","transfer.resume"})
            ui=subprocess.run([str(bin_dir/"cpnetflux-desktop"),"--socket",str(path),"--smoke-ms","600","--screenshot",str(root/"package.png")],
                env=env,capture_output=True,text=True,timeout=8)
            assert ui.returncode==0 and "connected=1" in ui.stderr,ui.stderr
            assert not any(v in ui.stderr for v in ["TypeError","ReferenceError","is not installed","Cannot assign"])
            assert agent.poll() is None
            print(json.dumps(dict(package=str(args.deb),files=len(files),adapter=info["adapter"],api=info["api_version"],
                core_discovery="installed sibling binaries",ui="offscreen passed",gnome_vm="not tested")))
        finally:
            agent.terminate();agent.communicate(timeout=5)
if __name__=="__main__":main()
