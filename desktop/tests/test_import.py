"""Exercise the real desktop node-package import path end to end."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary, agent = sys.argv[1:3]


def start_agent(root, env):
    runtime = root / "runtime"; runtime.mkdir(mode=0o700)
    agent_runtime = runtime / "cpnetflux"
    p = subprocess.Popen([sys.executable, agent, "--runtime-dir", str(agent_runtime),
                          "--state-dir", str(root/"state")], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    deadline = time.monotonic() + 5
    while not (agent_runtime / "agent.sock").exists() and time.monotonic() < deadline and p.poll() is None:
        time.sleep(.02)
    assert (agent_runtime / "agent.sock").exists(), "agent did not start"
    return p, agent_runtime / "agent.sock"


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    config = root / "config"; config.mkdir(mode=0o700)
    bundle = root / "lab-node.cpnetflux"
    token = "test-token-0123456789"
    ca = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n"
    bundle.write_text(json.dumps({
        "format": "cpnetflux-node-provisioning", "version": 1,
        "node": {"node_id": "lab-node", "name": "Lab", "host": "127.0.0.1",
                 "control_port": 22121, "server_name": "127.0.0.1",
                 "default_root_id": "home"},
        "tls": {"mode": "required", "server_name": "127.0.0.1", "ca_pem": ca},
        "credential": {"type": "token", "token": token},
        "security": {"sensitive": True},
    }), encoding="utf-8")
    bundle.chmod(0o600)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
               QT_QUICK_CONTROLS_STYLE="Fusion", XDG_RUNTIME_DIR=str(root/"runtime-root"),
               XDG_CONFIG_HOME=str(config))
    (root/"runtime-root").mkdir(mode=0o700)
    p, sock_path = start_agent(root, env)
    try:
        result = subprocess.run([binary, "--socket", str(sock_path), "--page", "3",
                                 "--import-bundle", str(bundle), "--smoke-ms", "1200"],
                                env=env, capture_output=True, text=True, timeout=8)
        assert result.returncode == 0, result.stderr
        profile_root = config / "cpnetflux" / "nodes"
        files = list(profile_root.glob("*/token"))
        cas = list(profile_root.glob("*/ca.pem"))
        assert len(files) == 1 and len(cas) == 1, (list(profile_root.rglob("*")), result.stderr)
        assert files[0].read_text() == token and cas[0].read_text() == ca
        assert files[0].stat().st_mode & 0o077 == 0 and cas[0].stat().st_mode & 0o077 == 0
        state = json.loads((root/"state"/"state.json").read_text())
        profile = state["profiles"]["lab-node"]
        assert profile["credential_ref"] == "file:" + str(files[0])
        assert token not in json.dumps(profile)
        assert token not in result.stdout + result.stderr

        bad = root / "group-writable.cpnetflux"
        bad_payload = json.loads(bundle.read_text())
        bad_payload["credential"]["token"] = "must-not-import"
        bad.write_text(json.dumps(bad_payload)); bad.chmod(0o660)
        result = subprocess.run([binary, "--socket", str(sock_path), "--page", "3",
                                 "--import-bundle", str(bad), "--smoke-ms", "500"],
                                env=env, capture_output=True, text=True, timeout=8)
        assert result.returncode == 0
        assert files[0].read_text() == token
        assert "must-not-import" not in files[0].read_text()
        print("desktop provisioning bundle import wrote private CA/token refs, rejected group-writable input, and avoided token leakage")
    finally:
        p.terminate()
        p.communicate(timeout=5)
