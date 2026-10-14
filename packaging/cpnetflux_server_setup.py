#!/usr/bin/env python3
"""CPNetFlux Linux server bootstrap and desktop provisioning bundle generator.

The command is intentionally explicit: it never enables a service, opens a
firewall, or prints credential material unless the operator requests it.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
from typing import Iterable

DATA_PORT_WINDOW_SIZE = 512
BUNDLE_FORMAT = "cpnetflux-node-provisioning"
BUNDLE_VERSION = 1
_SAFE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")


def parse_endpoint(value: str) -> tuple[str, int]:
    if not isinstance(value, str) or not value.strip():
        raise ValueError("endpoint is required")
    value = value.strip()
    if value.startswith("["):
        close = value.find("]")
        if close <= 1 or close + 2 > len(value) or value[close + 1] != ":":
            raise ValueError("IPv6 endpoint must use [address]:port")
        host = value[1:close]
        try:
            ipaddress.IPv6Address(host)
        except ValueError as exc:
            raise ValueError("bracketed endpoint must contain an IPv6 address") from exc
        port_text = value[close + 2:]
    else:
        if value.count(":") != 1:
            raise ValueError("endpoint must be host:port or [ipv6]:port")
        host, port_text = value.rsplit(":", 1)
    if not host or any(ch.isspace() for ch in host):
        raise ValueError("endpoint host is invalid")
    try:
        port = int(port_text, 10)
    except (TypeError, ValueError) as exc:
        raise ValueError("endpoint port is invalid") from exc
    if not 1024 <= port <= 65535:
        raise ValueError("control port must be between 1024 and 65535")
    return host, port


def data_port_range(base: int) -> tuple[int, int]:
    if type(base) is not int or not 1024 <= base <= 65535 - DATA_PORT_WINDOW_SIZE + 1:
        raise ValueError(
            f"data port base must be between 1024 and "
            f"{65535 - DATA_PORT_WINDOW_SIZE + 1}"
        )
    return base, base + DATA_PORT_WINDOW_SIZE - 1


def _validate_id(value: str, label: str) -> str:
    if not isinstance(value, str) or not _SAFE_ID.fullmatch(value) or ".." in value:
        raise ValueError(f"{label} must be a safe identifier")
    return value


def _validate_abs_path(value: str, label: str) -> Path:
    path = Path(value)
    if not path.is_absolute() or "\x00" in value or "\n" in value:
        raise ValueError(f"{label} must be an absolute path")
    return path


def _validate_bind_host(host: str) -> str:
    try:
        ipaddress.ip_address(host)
    except ValueError as exc:
        raise ValueError("listen host must be an IP address") from exc
    return host


def _validate_server_name(value: str) -> str:
    if not isinstance(value, str) or not value or any(
        ch.isspace() or ch in "/\\," for ch in value
    ):
        raise ValueError("tls server name is invalid")
    try:
        ipaddress.ip_address(value)
    except ValueError:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]{0,252}", value):
            raise ValueError("tls server name is invalid")
    return value


def _atomic_write(path: Path, content: bytes, mode: int, force: bool,
                  created: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    was_existing = path.exists()
    if was_existing and not force:
        raise FileExistsError(f"refusing to overwrite {path}; use --force")
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=str(path.parent))
    tmp = Path(tmp_name)
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
        if not was_existing and path not in created:
            created.append(path)
    finally:
        if tmp.exists():
            tmp.unlink()


def _ensure_dir(path: Path, mode: int, created: list[Path]) -> None:
    if path.exists():
        if path.is_symlink() or not path.is_dir():
            raise ValueError(f"directory is not a directory: {path}")
        return
    path.mkdir(parents=True, mode=mode)
    os.chmod(path, mode)
    created.append(path)


def _run(command: list[str], *, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True,
                          capture_output=capture)


def _openssl_generate(tls_dir: Path, server_name: str, force: bool,
                      created: list[Path]) -> tuple[Path, Path, Path]:
    """Generate a local CA and a server certificate with a SAN."""
    required = [tls_dir / "ca.key", tls_dir / "ca.crt",
                tls_dir / "server.key", tls_dir / "server.crt"]
    if any(path.exists() for path in required) and not force:
        raise FileExistsError("TLS material already exists; use --force to rotate it")
    staging = Path(tempfile.mkdtemp(prefix=".cpnetflux-tls-", dir=str(tls_dir)))
    try:
        ca_key = staging / "ca.key"
        ca_crt = staging / "ca.crt"
        server_key = staging / "server.key"
        csr = staging / "server.csr"
        server_crt = staging / "server.crt"
        _run(["openssl", "req", "-x509", "-newkey", "rsa:3072", "-sha256",
              "-nodes", "-days", "3650", "-keyout", str(ca_key),
              "-out", str(ca_crt), "-subj", "/CN=CPNetFlux local CA",
              "-addext", "basicConstraints=critical,CA:TRUE,pathlen:1",
              "-addext", "keyUsage=critical,keyCertSign,cRLSign",
              "-addext", "subjectKeyIdentifier=hash"])
        _run(["openssl", "req", "-new", "-newkey", "rsa:3072", "-nodes",
              "-keyout", str(server_key), "-out", str(csr),
              "-subj", f"/CN={server_name}"])
        try:
            ipaddress.ip_address(server_name)
            san = f"IP:{server_name}"
        except ValueError:
            san = f"DNS:{server_name}"
        ext = staging / "server.ext"
        ext.write_text(
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            "extendedKeyUsage=serverAuth\n"
            f"subjectAltName={san}\n"
            "subjectKeyIdentifier=hash\n",
            encoding="ascii",
        )
        _run(["openssl", "x509", "-req", "-sha256", "-days", "825",
              "-in", str(csr), "-CA", str(ca_crt), "-CAkey", str(ca_key),
              "-CAcreateserial", "-out", str(server_crt), "-extfile", str(ext)])
        outputs = []
        for source, target, mode in [
            (ca_key, tls_dir / "ca.key", 0o600),
            (ca_crt, tls_dir / "ca.crt", 0o644),
            (server_key, tls_dir / "server.key", 0o600),
            (server_crt, tls_dir / "server.crt", 0o644),
        ]:
            _atomic_write(target, source.read_bytes(), mode, force, created)
            outputs.append(target)
        return outputs[1], outputs[2], outputs[3]
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def build_bundle_payload(*, node_id: str, name: str, host: str, control_port: int,
                         server_name: str, default_root_id: str,
                         ca_pem: str, token: str) -> dict:
    node_id = _validate_id(node_id, "node_id")
    name = name.strip()
    if not name or len(name) > 128 or any(ch in "\r\n" for ch in name):
        raise ValueError("node name is invalid")
    if not isinstance(host, str) or not host or any(ch.isspace() for ch in host):
        raise ValueError("advertise host is invalid")
    if not 1024 <= int(control_port) <= 65535:
        raise ValueError("control port is invalid")
    _validate_server_name(server_name)
    _validate_id(default_root_id, "default_root_id")
    if "BEGIN CERTIFICATE" not in ca_pem or not token or "\n" in token:
        raise ValueError("CA certificate or token is invalid")
    return {
        "format": BUNDLE_FORMAT,
        "version": BUNDLE_VERSION,
        "node": {
            "node_id": node_id,
            "name": name,
            "host": host,
            "control_port": int(control_port),
            "server_name": server_name,
            "default_root_id": default_root_id,
        },
        "tls": {
            "mode": "required",
            "server_name": server_name,
            "ca_pem": ca_pem,
        },
        "credential": {
            "type": "token",
            "token": token,
        },
        "security": {
            "sensitive": True,
            "import_instructions": "Import only on the intended CPNetFlux desktop node; delete the bundle after import.",
        },
    }


def build_server_config(*, bind_host: str, port: int, data_port_base: int,
                        connections: int, root: Path, token_file: Path,
                        tls_key_file: Path, tls_cert_file: Path,
                        tls_ca_file: Path, access_policy: str) -> dict:
    _validate_bind_host(bind_host)
    data_port_range(data_port_base)
    if not 1 <= connections <= 8:
        raise ValueError("connections must be between 1 and 8")
    if access_policy not in {"read-only", "write-only", "read-write"}:
        raise ValueError("invalid access policy")
    return {
        "enabled": True,
        "host": bind_host,
        "port": int(port),
        "data_port_base": int(data_port_base),
        "connections": int(connections),
        "root": str(root),
        "token_file": str(token_file),
        "tls_key_file": str(tls_key_file),
        "tls_cert_file": str(tls_cert_file),
        "tls_ca_file": str(tls_ca_file),
        "tls_client_auth": "none",
        "access_policy": access_policy,
    }


def render_unit(*, data_root: Path, service_user: str) -> str:
    if not data_root.is_absolute() or "\n" in str(data_root):
        raise ValueError("data root is invalid")
    _validate_id(service_user, "service_user")
    return f"""[Unit]
Description=CPNetFlux authenticated transfer service
After=network.target
ConditionPathExists=/etc/cpnetflux/server.json

[Service]
Type=simple
User={service_user}
Group={service_user}
ExecStart=/usr/bin/cpnetflux-service --config /etc/cpnetflux/server.json
Restart=on-failure
RestartSec=5
UMask=0077
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths={data_root}
RestrictSUIDSGID=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
TimeoutStopSec=30

[Install]
WantedBy=multi-user.target
"""


def _read_token(path: Path) -> str:
    token = path.read_text(encoding="ascii").strip()
    if not token or "\n" in token or "\r" in token:
        raise ValueError("generated token is invalid")
    return token


def _chown(path: Path, user: str, group: str | None = None) -> None:
    import pwd
    import grp
    uid = pwd.getpwnam(user).pw_uid
    gid = grp.getgrnam(group or user).gr_gid
    os.chown(path, uid, gid)


def _ensure_service_user(name: str, created_user: list[bool]) -> None:
    import pwd
    try:
        pwd.getpwnam(name)
    except KeyError:
        _run(["useradd", "--system", "--home-dir", "/var/lib/cpnetflux",
              "--shell", "/usr/sbin/nologin", "--user-group", name])
        created_user.append(True)


def _check_control_port(host: str, port: int) -> None:
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    with socket.socket(family, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Initialize a CPNetFlux authenticated Linux server and node bundle"
    )
    parser.add_argument("--listen", default="0.0.0.0:2121",
                        help="bind endpoint, default 0.0.0.0:2121")
    parser.add_argument("--advertise-host",
                        help="address or DNS name written to the desktop bundle")
    parser.add_argument("--tls-server-name",
                        help="certificate SAN and TLS name (defaults to advertise host)")
    parser.add_argument("--data-port-base", type=int, default=20200)
    parser.add_argument("--connections", type=int, default=4)
    parser.add_argument("--data-root", default="/var/lib/cpnetflux/data")
    parser.add_argument("--config-dir", default="/etc/cpnetflux")
    parser.add_argument("--service-user", default="cpnetflux")
    parser.add_argument("--node-id", default="cpnetflux-node")
    parser.add_argument("--node-name", default="CPNetFlux node")
    parser.add_argument("--default-root-id", default="home")
    parser.add_argument("--access-policy",
                        choices=("read-only", "write-only", "read-write"),
                        default="read-write")
    parser.add_argument("--provisioning-bundle")
    parser.add_argument("--install-service", action="store_true")
    parser.add_argument("--enable", action="store_true")
    parser.add_argument("--start", action="store_true")
    parser.add_argument("--configure-firewall", action="store_true")
    parser.add_argument("--firewall-backend", choices=("auto", "ufw", "firewalld", "none"),
                        default="auto")
    parser.add_argument("--force", action="store_true",
                        help="allow replacement of existing generated files")
    parser.add_argument("--rotate-token", action="store_true",
                        help="generate a new token (requires --force when one exists)")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def _firewall_commands(backend: str, port: int, first_data: int, last_data: int) -> list[list[str]]:
    if backend == "none":
        return []
    if backend == "auto":
        if shutil.which("ufw"):
            backend = "ufw"
        elif shutil.which("firewall-cmd"):
            backend = "firewalld"
        else:
            raise ValueError("no supported firewall backend found; choose --firewall-backend none")
    if backend == "ufw":
        return [["ufw", "allow", f"{port}/tcp"],
                ["ufw", "allow", f"{first_data}:{last_data}/tcp"]]
    return [["firewall-cmd", "--permanent", f"--add-port={port}/tcp"],
            ["firewall-cmd", "--permanent", f"--add-port={first_data}-{last_data}/tcp"],
            ["firewall-cmd", "--reload"]]


def _print_plan(args, bind_host: str, port: int, first_data: int, last_data: int,
                config: Path, bundle: Path) -> None:
    payload = {
        "listen": f"{bind_host}:{port}",
        "data_port_window": f"{first_data}-{last_data}",
        "config": str(config),
        "bundle": str(bundle),
        "install_service": bool(args.install_service),
        "enable": bool(args.enable),
        "start": bool(args.start),
        "configure_firewall": bool(args.configure_firewall),
        "writes_credentials": True,
        "dry_run": True,
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def main(argv: Iterable[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        bind_host, port = parse_endpoint(args.listen)
        _validate_bind_host(bind_host)
        first_data, last_data = data_port_range(args.data_port_base)
        advertise_host = args.advertise_host or args.tls_server_name or bind_host
        if advertise_host in {"0.0.0.0", "::"}:
            raise ValueError("wildcard listen address cannot be used in a node bundle; provide --advertise-host")
        _validate_server_name(args.tls_server_name or advertise_host)
        tls_server_name = args.tls_server_name or advertise_host
        _validate_server_name(tls_server_name)
        _validate_id(args.node_id, "node_id")
        _validate_id(args.default_root_id, "default_root_id")
        _validate_id(args.service_user, "service_user")
        config_dir = _validate_abs_path(args.config_dir, "config_dir")
        data_root = _validate_abs_path(args.data_root, "data_root")
        if args.enable or args.start:
            if not args.install_service:
                raise ValueError("--enable/--start require --install-service")
        config_path = config_dir / "server.json"
        tls_dir = config_dir / "tls"
        token_path = config_dir / "token"
        bundle_path = Path(args.provisioning_bundle or
                           f"/root/cpnetflux-provision/{args.node_id}.cpnetflux")
        bundle_path = _validate_abs_path(str(bundle_path), "provisioning_bundle")
        if args.dry_run:
            _print_plan(args, bind_host, port, first_data, last_data, config_path, bundle_path)
            return 0
        if os.geteuid() != 0:
            raise PermissionError("server setup requires root; use --dry-run to inspect")
        if args.configure_firewall:
            firewall_cmds = _firewall_commands(args.firewall_backend, port, first_data, last_data)
        else:
            firewall_cmds = []
        _check_control_port(bind_host, port)
        created: list[Path] = []
        created_user: list[bool] = []
        try:
            _ensure_service_user(args.service_user, created_user)
            _ensure_dir(config_dir, 0o750, created)
            _ensure_dir(tls_dir, 0o750, created)
            _ensure_dir(data_root, 0o750, created)
            _ensure_dir(bundle_path.parent, 0o700, created)
            ca_path, key_path, cert_path = _openssl_generate(
                tls_dir, tls_server_name, args.force, created)
            if token_path.exists() and not args.force and args.rotate_token:
                raise FileExistsError("token already exists; use --force with --rotate-token")
            token = secrets.token_urlsafe(32) if args.rotate_token or not token_path.exists() else _read_token(token_path)
            if not token_path.exists() or args.force and args.rotate_token:
                _atomic_write(token_path, (token + "\n").encode("ascii"), 0o600, args.force, created)
            config = build_server_config(
                bind_host=bind_host, port=port, data_port_base=args.data_port_base,
                connections=args.connections, root=data_root, token_file=token_path,
                tls_key_file=key_path, tls_cert_file=cert_path, tls_ca_file=ca_path,
                access_policy=args.access_policy)
            _atomic_write(config_path, (json.dumps(config, indent=2) + "\n").encode(),
                          0o640, args.force, created)
            if args.install_service:
                unit_path = Path("/etc/systemd/system/cpnetflux-server.service")
                unit = render_unit(data_root=data_root, service_user=args.service_user)
                _atomic_write(unit_path, unit.encode(), 0o644, args.force, created)
            ca_pem = ca_path.read_text(encoding="ascii")
            bundle = build_bundle_payload(
                node_id=args.node_id, name=args.node_name, host=advertise_host,
                control_port=port, server_name=tls_server_name,
                default_root_id=args.default_root_id, ca_pem=ca_pem, token=token)
            _atomic_write(bundle_path, (json.dumps(bundle, indent=2) + "\n").encode(),
                          0o600, args.force, created)
            # Keep policy/configuration root-owned; only the daemon inputs and data
            # are writable/readable by the dedicated service account.
            _chown(data_root, args.service_user)
            _chown(token_path, args.service_user)
            _chown(key_path, args.service_user)
            os.chown(config_dir, 0, __import__("grp").getgrnam(args.service_user).gr_gid)
            os.chown(tls_dir, 0, __import__("grp").getgrnam(args.service_user).gr_gid)
            os.chown(config_path, 0, __import__("grp").getgrnam(args.service_user).gr_gid)
            os.chown(ca_path, 0, __import__("grp").getgrnam(args.service_user).gr_gid)
            os.chown(cert_path, 0, __import__("grp").getgrnam(args.service_user).gr_gid)
            os.chmod(config_dir, 0o750)
            os.chmod(tls_dir, 0o750)
            os.chmod(data_root, 0o750)
            os.chmod(token_path, 0o600)
            os.chmod(config_path, 0o640)
            os.chmod(key_path, 0o600)
            os.chmod(ca_path, 0o640)
            os.chmod(cert_path, 0o640)
            if args.install_service:
                _run(["systemctl", "daemon-reload"])
                if args.enable:
                    _run(["systemctl", "enable", "cpnetflux-server.service"])
                if args.start:
                    _run(["systemctl", "start", "cpnetflux-server.service"])
            for command in firewall_cmds:
                _run(command)
            digest = hashlib.sha256(bundle_path.read_bytes()).hexdigest()
            print(f"CPNetFlux server initialized: {config_path}")
            print(f"TLS+token node bundle: {bundle_path}")
            print(f"Bundle SHA256: {digest}")
            print(f"Control TCP: {port}; passive data TCP window: {first_data}-{last_data}")
            print("Token material was not printed; transfer the bundle over a secure channel and delete it after import.")
            return 0
        except Exception:
            for path in reversed(created):
                try:
                    if path.is_file() or path.is_symlink():
                        path.unlink()
                    elif path.is_dir():
                        path.rmdir()
                except OSError:
                    pass
            if created_user:
                try:
                    _run(["userdel", args.service_user])
                except Exception:
                    pass
            raise
    except (ValueError, FileExistsError, PermissionError, OSError,
            subprocess.CalledProcessError) as exc:
        print(f"cpnetflux-server-setup: refused: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
