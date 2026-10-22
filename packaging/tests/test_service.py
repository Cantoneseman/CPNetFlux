"""Deployment entrypoint must refuse incomplete or unsafe configuration."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("service", Path(__file__).resolve().parents[1]/"cpnetflux_service.py")
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

class ServiceSafety(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for name in ["token", "key", "cert", "ca"]:
            (self.root/name).write_text("synthetic fixture")
            (self.root/name).chmod(0o600)
        (self.root/"data").mkdir()
        self.config = dict(enabled=True, host="127.0.0.1", port=2121, data_port_base=20200,
            connections=4, root=str(self.root/"data"), token_file=str(self.root/"token"),
            tls_key_file=str(self.root/"key"), tls_cert_file=str(self.root/"cert"),
            tls_ca_file=str(self.root/"ca"), access_policy="read-only")
        self.path = self.root/"server.json"
    def tearDown(self): self.tmp.cleanup()
    def argv(self):
        self.path.write_text(json.dumps(self.config)); self.path.chmod(0o600)
        return service.load_options(self.path)
    def test_generated_token_tls_policy_allows_desktop_without_client_certificate(self):
        self.config["tls_client_auth"] = "none"
        argv = self.argv()
        self.assertEqual(argv[argv.index("--tls-client-auth") + 1], "none")

    def test_user_agent_uses_relocatable_wrapper_and_shared_user_paths(self):
        packaging=Path(__file__).resolve().parents[1]
        unit=(packaging/"cpnetflux-agent.service").read_text()
        self.assertIn("ExecStart=/usr/bin/cpnetflux-agent",unit)
        self.assertNotIn("PrivateTmp=true",unit)
        self.assertNotIn(".local/lib/cpnetflux",unit)
        wrapper=(packaging.parent/"desktop/agent/cpnetflux-agent.in").read_text()
        self.assertIn('--core-bin-dir',wrapper)
        self.assertIn('CPNETFLUX_CORE_BIN_DIR:-$base/@CMAKE_INSTALL_BINDIR@',wrapper)

    def test_secure_policy_is_explicit(self):
        argv = self.argv()
        for flag, value in [("--tls-mode","required"),("--tls-client-auth","required"),
                            ("--data-tls-mode","required"),("--auth-mode","token"),
                            ("--checksum","crc32c"),("--commit-sync-policy","fsync_file_and_dir")]:
            self.assertEqual(argv[argv.index(flag)+1], value)
        self.assertNotIn("synthetic fixture", argv)
    def test_disabled_or_missing_credentials_fail(self):
        self.config["enabled"] = False
        with self.assertRaises(ValueError): self.argv()
        self.config["enabled"] = True
        del self.config["token_file"]
        with self.assertRaises(ValueError): self.argv()
    def test_unknown_and_invalid_fields_fail(self):
        for key, value in [("password","anything"),("port",True),("connections",9),
                           ("host","0.0.0.0;echo"),("access_policy","anonymous")]:
            old = dict(self.config); self.config[key] = value
            with self.assertRaises(ValueError): self.argv()
            self.config = old
    def test_symlink_and_shared_secret_fail(self):
        (self.root/"token").chmod(0o644)
        with self.assertRaises(ValueError): self.argv()
        (self.root/"token").chmod(0o600)
        (self.root/"link").symlink_to(self.root/"token")
        self.config["token_file"] = str(self.root/"link")
        with self.assertRaises((ValueError,OSError)): self.argv()
    def test_shared_writable_config_fail(self):
        self.argv(); self.path.chmod(0o666)
        with self.assertRaises(ValueError): service.load_options(self.path)
    def test_symlink_config_fail(self):
        self.argv(); link = self.root/"link"; link.symlink_to(self.path)
        with self.assertRaises((ValueError,OSError)): service.load_options(link)

if __name__ == "__main__": unittest.main()
