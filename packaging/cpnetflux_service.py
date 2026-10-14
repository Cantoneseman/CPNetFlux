#!/usr/bin/env python3
"""Fail-closed deployment launcher. Configuration contains paths, never secrets."""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import stat

FIELDS = {"enabled", "host", "port", "data_port_base", "connections", "root",
          "token_file", "tls_key_file", "tls_cert_file", "tls_ca_file", "access_policy"}
OPTIONAL_FIELDS = {"tls_client_auth"}

def check_file(path, private=False):
    if not isinstance(path, str) or not Path(path).is_absolute():
        raise ValueError("absolute file path required")
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size == 0:
            raise ValueError("regular nonempty file required")
        if info.st_uid not in {0, os.geteuid()} or info.st_mode & 0o022:
            raise ValueError("untrusted file owner or permissions")
        if private and (info.st_uid != os.geteuid() or info.st_mode & 0o077):
            raise ValueError("secrets must be owned by the service user and private")
    finally:
        os.close(fd)

def load_options(path):
    check_file(str(path))
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd) as stream:
        if os.fstat(stream.fileno()).st_size > 65536:
            raise ValueError("config exceeds budget")
        c = json.load(stream)
    if not isinstance(c, dict) or not FIELDS.issubset(c) or set(c) - FIELDS - OPTIONAL_FIELDS or c["enabled"] is not True:
        raise ValueError("incomplete, disabled or unknown config")
    if not isinstance(c["host"], str):
        raise ValueError("IP address string required")
    ipaddress.ip_address(c["host"])
    for key, low, high in [("port",1024,65535),("data_port_base",1024,65024),("connections",1,8)]:
        if type(c[key]) is not int or not low <= c[key] <= high:
            raise ValueError("invalid numeric setting")
    tls_client_auth = c.get("tls_client_auth", "required")
    if tls_client_auth not in {"none", "required"}:
        raise ValueError("invalid tls client auth policy")
    if c["access_policy"] not in {"read-only","write-only","read-write"}:
        raise ValueError("invalid access policy")
    if not isinstance(c["root"],str) or not Path(c["root"]).is_absolute():
        raise ValueError("absolute data root required")
    root = Path(c["root"])
    if root.is_symlink() or not root.is_dir():
        raise ValueError("data root must be an existing directory")
    for key in ["token_file","tls_key_file","tls_cert_file","tls_ca_file"]:
        check_file(c[key], private=key in {"token_file","tls_key_file"})
    options = ["--tls-mode","required","--tls-client-auth",tls_client_auth,"--data-tls-mode","required",
               "--auth-mode","token","--checksum","crc32c","--final-verify-policy","full",
               "--commit-sync-policy","fsync_file_and_dir"]
    for key in ["host","port","data_port_base","connections","root","access_policy",
                "token_file","tls_key_file","tls_cert_file","tls_ca_file"]:
        flag = "--auth-token-file" if key == "token_file" else "--"+key.replace("_","-")
        options += [flag,str(c[key])]
    return options

def main():
    parser = argparse.ArgumentParser(description="CPNetFlux secure service launcher")
    parser.add_argument("--config", type=Path, default=Path("/etc/cpnetflux/server.json"))
    parser.add_argument("--check-config", action="store_true")
    args = parser.parse_args()
    try:
        options = load_options(args.config)
        if args.check_config:
            print("configuration accepted; certificate/token validity is checked by the server")
            return
        if os.geteuid() == 0:
            raise ValueError("service must run as a dedicated non-root user")
    except (ValueError, OSError, TypeError):
        parser.exit(1,"CPNetFlux service refused unsafe, disabled or incomplete configuration\n")
    binary = Path(__file__).resolve().parents[2]/"bin/cpnetflux-gridftp-server"
    os.execv(binary,[str(binary),*options])

if __name__ == "__main__": main()
