"""Real Qt offscreen startup/render checks, not a physical desktop UX claim."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary, agent = sys.argv[1:3]
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    runtime = root / "runtime"; runtime.mkdir(mode=0o700)
    agent_runtime = runtime / "cpnetflux"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", QT_QUICK_CONTROLS_STYLE="Fusion", XDG_RUNTIME_DIR=str(runtime))
    p = subprocess.Popen([sys.executable, agent, "--demo", "--runtime-dir", str(agent_runtime), "--state-dir", str(root/"state"), "--tick-ms", "100"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        path = agent_runtime/"agent.sock"
        end=time.monotonic()+5
        while not path.exists() and time.monotonic()<end: time.sleep(.02)
        assert path.exists(), "agent did not start"
        fixtures=json.loads((Path(agent).resolve().parents[1]/"tests/fixtures.json").read_text())
        params=next(v["message"]["params"] for v in fixtures["valid"] if v["name"]=="transfer.create")
        s=socket.socket(socket.AF_UNIX); s.connect(str(path)); stream=s.makefile("rb")
        s.sendall((json.dumps(dict(jsonrpc="2.0",id="ui-task",method="transfer.create",params=params))+"\n").encode())
        assert json.loads(stream.readline())["result"]["snapshot"]["state"]=="queued"
        stream.close(); s.close()
        for page in range(7):
            screenshot=root/(str(page)+".png")
            result=subprocess.run([binary,"--socket",str(path),"--page",str(page),"--smoke-ms","900","--screenshot",str(screenshot)], env=env, capture_output=True, text=True, timeout=8)
            assert result.returncode==0, str(result.returncode)+result.stderr
            assert "snapshots=1" in result.stderr, result.stderr
            for marker in ["TypeError", "ReferenceError", "is not installed", "Cannot assign", "Binding loop", "failed to load"]:
                assert marker not in result.stderr, result.stderr
            if screenshot.exists() and screenshot.stat().st_size:
                assert screenshot.read_bytes()[:8]==b"\x89PNG\r\n\x1a\n"
        # Closing seven UI processes must not cancel the queued agent task.
        assert p.poll() is None, "UI shutdown killed agent"
        print("7 Qt pages rendered, subscription synced, UI exit kept agent alive")
    finally:
        p.terminate(); p.communicate(timeout=5)
