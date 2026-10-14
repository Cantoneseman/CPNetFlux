"""Behavioral tests for safe server bootstrap and node provisioning."""
import importlib.util
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "cpnetflux_server_setup.py"
spec = importlib.util.spec_from_file_location("server_setup", MODULE)
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class ServerSetupContract(unittest.TestCase):
    def test_parse_endpoint_accepts_ipv4_hostname_and_bracketed_ipv6(self):
        self.assertEqual(setup.parse_endpoint("0.0.0.0:2121"), ("0.0.0.0", 2121))
        self.assertEqual(setup.parse_endpoint("node.example:4433"), ("node.example", 4433))
        self.assertEqual(setup.parse_endpoint("[2001:db8::1]:2121"), ("2001:db8::1", 2121))

    def test_parse_endpoint_rejects_ambiguous_or_privileged_ports(self):
        for value in ("0.0.0.0", "host:x", "host:22", "host:65536", "[not-ip]:2121"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                setup.parse_endpoint(value)

    def test_passive_range_matches_current_server_window(self):
        self.assertEqual(setup.data_port_range(20200), (20200, 20711))
        with self.assertRaises(ValueError):
            setup.data_port_range(65025)

    def test_wildcard_address_requires_an_advertised_endpoint(self):
        self.assertEqual(setup.main(["--dry-run", "--listen", "0.0.0.0:2121"]), 1)

    def test_bundle_contains_connection_details_ca_and_token(self):
        payload = setup.build_bundle_payload(
            node_id="lab-node", name="Lab", host="transfer.example",
            control_port=2121, server_name="transfer.example",
            default_root_id="home", ca_pem="-----BEGIN CERTIFICATE-----\npublic\n",
            token="secret-token"
        )
        self.assertEqual(payload["format"], "cpnetflux-node-provisioning")
        self.assertEqual(payload["version"], 1)
        self.assertEqual(payload["node"]["host"], "transfer.example")
        self.assertEqual(payload["node"]["server_name"], "transfer.example")
        self.assertIn("secret-token", payload["credential"]["token"])
        self.assertIn("BEGIN CERTIFICATE", payload["tls"]["ca_pem"])

    def test_bundle_rejects_invalid_identity_and_endpoint(self):
        with self.assertRaises(ValueError):
            setup.build_bundle_payload(
                node_id="../escape", name="Lab", host="localhost",
                control_port=2121, server_name="localhost", default_root_id="home",
                ca_pem="ca", token="token"
            )


if __name__ == "__main__":
    unittest.main()
