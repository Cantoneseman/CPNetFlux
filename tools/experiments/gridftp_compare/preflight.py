from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import time
from pathlib import Path
from urllib.parse import quote

from tools.release import remote_auth
from tools.test.gridftp_port_window import assert_epsv_port_in_window, passive_data_port_window_end

from .dataset import file_sha256, make_dataset
from .schemas import PreflightResult, STATUS_BLOCKED_EXTERNAL_GRIDFTP, STATUS_DRY_RUN, STATUS_PASS


REPO_ROOT = Path(__file__).resolve().parents[3]


def utc_timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def command_text(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def file_url(path: Path, *, directory: bool = False) -> str:
    resolved = path.resolve().as_uri()
    if directory and not resolved.endswith("/"):
        return resolved + "/"
    return resolved


def gridftp_url(
    host: str,
    port: int,
    remote_path: str | Path,
    *,
    directory: bool = False,
    auth_mode: str = "anonymous",
    home_dir: str | Path | None = None,
) -> str:
    text = str(remote_path)
    if auth_mode == "anonymous":
        if home_dir is None:
            raise ValueError("home_dir is required for anonymous GridFTP URLs")
        root = Path(home_dir).resolve()
        candidate = Path(text).resolve()
        try:
            text = candidate.relative_to(root).as_posix()
        except ValueError as exc:
            raise ValueError(f"anonymous GridFTP path is outside home_dir: {text}") from exc
    elif auth_mode == "gsi":
        # GSI servers resolve URLs relative to their configured home directory.
        # Keep the URL relative when the caller supplies an absolute filesystem path.
        if home_dir:
            root = Path(home_dir).resolve()
            candidate = Path(text)
            if candidate.is_absolute():
                try:
                    text = candidate.resolve().relative_to(root).as_posix()
                except ValueError as exc:
                    raise ValueError(f"GSI GridFTP path is outside home_dir: {text}") from exc
        text = text.lstrip("/")
    else:
        raise ValueError(f"unsupported GridFTP auth mode: {auth_mode}")
    encoded = quote(text) if auth_mode == "anonymous" else quote(text, safe="/")
    if not encoded.startswith("/"):
        encoded = "/" + encoded
    if directory and not encoded.endswith("/"):
        encoded += "/"
    scheme = "ftp" if auth_mode == "anonymous" else "gsiftp"
    userinfo = "anonymous@" if auth_mode == "anonymous" else ""
    return f"{scheme}://{userinfo}{host}:{port}{encoded}"


def build_globus_transfer_command(
    *,
    globus_url_copy: str,
    source_url: str,
    dest_url: str,
    parallelism: int | None,
    concurrency: int = 1,
    recursive: bool = False,
    restart: bool = False,
    auth_mode: str = "anonymous",
    fast: bool = True,
) -> list[str]:
    if auth_mode == "anonymous":
        data_channel_flag = "-nodcau"
    elif auth_mode == "gsi":
        data_channel_flag = "-dcpriv"
    else:
        raise ValueError(f"unsupported GridFTP auth mode: {auth_mode}")
    command = [globus_url_copy]
    if fast:
        command.append("-fast")
    command.extend([data_channel_flag, "-cd", "-rp"])
    if parallelism is not None:
        command.extend(["-p", str(parallelism)])
    if recursive:
        command.extend(["-r", "-cc", str(concurrency)])
    if restart:
        command.append("-rst")
    command.extend([source_url, dest_url])
    return command


def build_globus_partial_get_command(
    *,
    globus_url_copy: str,
    source_url: str,
    dest_url: str,
    offset: int,
    length: int,
    auth_mode: str,
) -> list[str]:
    if offset < 0 or length <= 0:
        raise ValueError("partial transfer offset must be non-negative and length must be positive")
    command = build_globus_transfer_command(
        globus_url_copy=globus_url_copy,
        source_url=source_url,
        dest_url=dest_url,
        parallelism=None,
        auth_mode=auth_mode,
        fast=False,
    )
    return [*command[:-2], "-off", str(offset), "-len", str(length), *command[-2:]]


def run_remote_capture(remote: str, command: str, *, timeout: int = 30) -> subprocess.CompletedProcess[str]:
    env = remote_auth.command_env(remote, REPO_ROOT)
    return subprocess.run(
        remote_auth.ssh_prefix(remote, root=REPO_ROOT) + [command],
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
        env=env,
    )


def _version_candidates(args: list[str]) -> list[list[str]]:
    candidates = [args, ["-version"], ["-versions"], ["--version"], ["-help"]]
    unique: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()
    for candidate in candidates:
        key = tuple(candidate)
        if key not in seen:
            unique.append(candidate)
            seen.add(key)
    return unique


def _is_usable_version_line(command: str, text: str, returncode: int) -> bool:
    if not text:
        return False
    lower = text.lower()
    if "not found" in lower or "double-dashed option syntax is not allowed" in lower:
        return False
    return returncode == 0 or command in text or bool(re.search(r"\d+\.\d+", text))


def local_version(command: str, args: list[str], *, timeout: int = 10) -> tuple[bool, str]:
    executable = shutil.which(command)
    if executable is None:
        return False, f"{command} not found"
    last_text = ""
    last_returncode = 0
    for candidate in _version_candidates(args):
        completed = subprocess.run([executable, *candidate], text=True, capture_output=True, check=False, timeout=timeout)
        text = (completed.stdout + completed.stderr).strip()
        first_line = text.splitlines()[0] if text else ""
        last_text = first_line
        last_returncode = completed.returncode
        if _is_usable_version_line(command, first_line, completed.returncode):
            return True, first_line
    return False, last_text or f"{command} version check failed with exit_code={last_returncode}"


def remote_version(remote: str, command: str, args: list[str], *, timeout: int = 20) -> tuple[bool, str]:
    probes = [
        " ".join(shlex.quote(part) for part in [command, *candidate])
        for candidate in _version_candidates(args)
    ]
    shell = [
        f"if ! command -v {shlex.quote(command)} >/dev/null 2>&1; then echo {shlex.quote(command + ' not found')}; exit 127; fi",
        "last=''",
        "last_rc=0",
    ]
    for probe in probes:
        shell.extend(
            [
                f"out=$({probe} 2>&1)",
                "rc=$?",
                "line=$(printf '%s\\n' \"$out\" | sed -n '1p')",
                "last=$line",
                "last_rc=$rc",
                "case \"$line\" in",
                "  *'double-dashed option syntax is not allowed'*|'') ;;",
                "  *) printf '%s\\n' \"$line\"; exit 0 ;;",
                "esac",
            ]
        )
    shell.append("printf '%s\\n' \"$last\"")
    shell.append("exit \"$last_rc\"")
    completed = run_remote_capture(
        remote,
        "\n".join(shell),
        timeout=timeout,
    )
    text = (completed.stdout + completed.stderr).strip()
    if completed.returncode != 0:
        return False, text or f"{command} remote check failed with exit_code={completed.returncode}"
    return True, text.splitlines()[0] if text else command


def check_tcp_connect(host: str, port: int, *, timeout: float = 5.0) -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True, ""
    except OSError as exc:
        return False, str(exc)


def write_preflight_json(path: Path, result: PreflightResult, checks: dict[str, object]) -> dict[str, object]:
    payload: dict[str, object] = result.to_dict()
    payload["checks"] = checks
    payload["generated_at"] = utc_timestamp()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return payload


def run_gridftp_preflight(
    *,
    output_dir: Path,
    control_host: str,
    control_port: int,
    data_port_base: int,
    remote: str = "",
    auth_mode: str = "anonymous",
    run_smoke: bool = False,
    dry_run: bool = False,
    globus_url_copy: str = "globus-url-copy",
    globus_gridftp_server: str = "globus-gridftp-server",
    remote_path_prefix: str = "/tmp/cpnetflux-gridftp-compare",
    gridftp_home_dir: str = "",
    server_check: dict[str, object] | None = None,
    timeout: int = 300,
) -> dict[str, object]:
    end_port = passive_data_port_window_end(data_port_base)
    assert_epsv_port_in_window(data_port_base, data_port_base)
    checks: dict[str, object] = {
        "local_globus_url_copy": {},
        "server_globus_gridftp": {},
        "control_tcp": {},
        "smoke": {},
        "server": server_check or {},
    }
    client_log = output_dir / "gridftp_preflight_client.log"
    server_log = output_dir / "gridftp_preflight_server.log"
    status = STATUS_PASS
    errors: list[str] = []

    if dry_run:
        result = PreflightResult(
            status=STATUS_DRY_RUN,
            tool=globus_url_copy,
            auth_mode=auth_mode,
            control_host=control_host,
            control_port=control_port,
            data_port_range=f"{data_port_base}..{end_port}",
            client_log=str(client_log),
            server_log=str(server_log),
        )
        return write_preflight_json(output_dir / "preflight.json", result, checks)

    if server_check is not None and not bool(server_check.get("ok", False)):
        status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
        errors.append(str(server_check.get("error", "standalone GridFTP server was not ready")))

    ok, text = local_version(globus_url_copy, ["--version"])
    checks["local_globus_url_copy"] = {"ok": ok, "detail": text}
    if not ok:
        status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
        errors.append(text)

    if remote:
        ok, server_text = remote_version(remote, globus_gridftp_server, ["-version"])
    else:
        ok, server_text = local_version(globus_gridftp_server, ["-version"])
    checks["server_globus_gridftp"] = {"ok": ok, "detail": server_text, "remote": remote}
    if not ok:
        status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
        errors.append(server_text)

    ok, tcp_error = check_tcp_connect(control_host, control_port)
    checks["control_tcp"] = {"ok": ok, "host": control_host, "port": control_port, "error": tcp_error}
    if not ok:
        status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
        errors.append(f"control port unavailable: {tcp_error}")

    smoke_hash_match = False
    if status == STATUS_PASS and run_smoke:
        smoke_dir = output_dir / "gridftp_preflight_smoke"
        manifest = make_dataset(smoke_dir / "source", profile="single_64MiB", materialize=True)
        source = smoke_dir / "source" / "single.bin"
        download = smoke_dir / "download.bin"
        remote_root = f"{remote_path_prefix.rstrip('/')}/preflight/{int(time.time())}"
        remote_file = f"{remote_root}/single.bin"
        home_dir = gridftp_home_dir or remote_path_prefix
        if remote:
            remote_dir = str(Path(remote_file).parent)
            mkdir = run_remote_capture(
                remote,
                f"mkdir -p {shlex.quote(remote_dir)} && chmod 777 {shlex.quote(remote_dir)}",
                timeout=30,
            )
            checks["smoke_mkdir"] = {"exit_code": mkdir.returncode, "stderr": mkdir.stderr.strip()}
            if mkdir.returncode != 0:
                status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
                errors.append("remote smoke mkdir failed: " + mkdir.stderr.strip())
        if status == STATUS_PASS:
            upload = build_globus_transfer_command(
                globus_url_copy=globus_url_copy,
                source_url=file_url(source),
                dest_url=gridftp_url(
                    control_host,
                    control_port,
                    remote_file,
                    auth_mode=auth_mode,
                    home_dir=home_dir,
                ),
                parallelism=1,
                auth_mode=auth_mode,
            )
            download_cmd = build_globus_transfer_command(
                globus_url_copy=globus_url_copy,
                source_url=gridftp_url(
                    control_host,
                    control_port,
                    remote_file,
                    auth_mode=auth_mode,
                    home_dir=home_dir,
                ),
                dest_url=file_url(download),
                parallelism=None if auth_mode == "gsi" else 1,
                fast=auth_mode != "gsi",
                auth_mode=auth_mode,
            )
            with client_log.open("w", encoding="utf-8") as handle:
                handle.write(
                    f"auth_mode={auth_mode} data_channel_protection={'private' if auth_mode == 'gsi' else 'none'} "
                    f"control_host={control_host} control_port={control_port} data_port_range={data_port_base}..{end_port}\n"
                )
                handle.write("$ " + command_text(upload) + "\n")
                started = time.monotonic()
                uploaded = subprocess.run(
                    upload,
                    text=True,
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    check=False,
                    timeout=timeout,
                    env=os.environ.copy(),
                )
                handle.write(f"\nexit_code={uploaded.returncode}\n")
                handle.write("$ " + command_text(download_cmd) + "\n")
                download_env = os.environ.copy()
                if auth_mode == "gsi":
                    download_env["GLOBUS_FTP_CLIENT_SOURCE_PASV"] = "1"
                downloaded = subprocess.run(
                    download_cmd,
                    text=True,
                    stdout=handle,
                    stderr=subprocess.STDOUT,
                    check=False,
                    timeout=timeout,
                    env=download_env,
                )
                handle.write(f"\nexit_code={downloaded.returncode} elapsed={time.monotonic() - started:.6f}\n")
            smoke_hash_match = (
                uploaded.returncode == 0
                and downloaded.returncode == 0
                and download.is_file()
                and file_sha256(source) == file_sha256(download)
                and manifest["single"]["sha256"] == file_sha256(download)
            )
            checks["smoke"] = {
                "ok": smoke_hash_match,
                "source": str(source),
                "remote_file": remote_file,
                "download": str(download),
                "upload_command": command_text(upload),
                "download_command": command_text(download_cmd),
                "uploaded_exit_code": uploaded.returncode,
                "downloaded_exit_code": downloaded.returncode,
                "source_sha256": file_sha256(source),
                "download_sha256": file_sha256(download) if download.is_file() else "",
                "source_bytes": source.stat().st_size,
                "download_bytes": download.stat().st_size if download.is_file() else 0,
            }
            if not smoke_hash_match:
                status = STATUS_BLOCKED_EXTERNAL_GRIDFTP
                errors.append("GridFTP STOR/RETR smoke did not complete with matching SHA-256")
    elif status == STATUS_PASS:
        checks["smoke"] = {"ok": False, "skipped": True, "reason": "run_smoke was not requested"}

    result = PreflightResult(
        status=status,
        tool=globus_url_copy,
        tool_version=str(checks["local_globus_url_copy"].get("detail", "")),
        server_version=str(checks["server_globus_gridftp"].get("detail", "")),
        auth_mode=auth_mode,
        control_host=control_host,
        control_port=control_port,
        data_port_range=f"{data_port_base}..{end_port}",
        smoke_hash_match=smoke_hash_match,
        server_log=str(server_log),
        client_log=str(client_log),
        error="; ".join(errors),
    )
    return write_preflight_json(output_dir / "preflight.json", result, checks)
