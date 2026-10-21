#!/usr/bin/env python3
"""CPNetFlux local shell agent. Demo never opens or transfers user files."""
import argparse
import asyncio
from collections import deque
import copy
from datetime import datetime, timezone
import fcntl
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import struct
import tempfile
import uuid
from jsonschema import Draft7Validator, ValidationError, FormatChecker

MAX_FRAME = 1024 * 1024
MAX_TASKS = 200
MAX_OPERATIONS = 2000
MAX_CLIENTS = 16
MAX_PAGE_SNAPSHOTS = 64
MAX_PAGE_CURSORS = 512
API_VERSION = "3.0.0"
PAGE_TTL_SECONDS = 300
TERMINAL = {"completed", "failed", "cancelled", "recoverable"}
ACTIVE = {"scanning", "connecting", "negotiating", "transferring", "committing"}
ERROR_NUMBERS = {"unsupported":-32001, "invalid_state":-32002,
 "revision_conflict":-32002, "not_found":-32003, "auth_failed":-32004,
 "keyring_locked":-32004, "tls_identity_failed":-32005,
 "permission_denied":-32006, "data_port_unreachable":-32007,
 "capability_mismatch":-32008, "checkpoint_missing":-32009,
 "source_changed":-32010, "cursor_expired":-32011,
 "protocol_error":-32012, "internal_error":-32603}

def uid(): return str(uuid.uuid4())
def now(): return datetime.now(timezone.utc).isoformat()
def empty_evidence():
    return dict(phase_timing=dict(control_prepare=None, data_connect=None, first_payload=None,
                                  payload_io=None, transfer_complete_wait=None),
                manifest=None, checksum=None, finalize=None, event_log=None, summary=None)

def detail(code, message, transfer_id=None, scope="rpc"):
    return dict(code=code, scope=scope, message=message, retryable=False,
                transfer_id=transfer_id, file_id=None, range_id=None)
class RpcError(Exception):
    def __init__(self, code, message, number=None):
        super().__init__(message)
        self.code, self.message = code, message
        self.number = number or ERROR_NUMBERS[code]

def private_dir(path):
    path = Path(path).absolute()
    if path.is_symlink(): raise RuntimeError("private directory cannot be a symlink")
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    info = path.stat()
    if info.st_uid != os.getuid(): raise RuntimeError("private directory owner mismatch")
    if info.st_mode & 0o077: raise RuntimeError("private directory must have mode 0700")
    return path

class Agent:
    def __init__(self, args, schema):
        self.args = args
        self.runtime = private_dir(args.runtime_dir)
        self.state_dir = private_dir(args.state_dir)
        self.lock = open(self.runtime / "agent.lock", "a+")
        os.chmod(self.runtime / "agent.lock", 0o600)
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.state_lock = open(self.state_dir / "state.lock", "a+")
        os.chmod(self.state_dir / "state.lock", 0o600)
        fcntl.flock(self.state_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.schema = schema
        self.validator = Draft7Validator(dict(schema, **{"$ref":"#/$defs/request"}), format_checker=FormatChecker())
        self.epoch, self.sequence = uid(), 0
        self.events, self.clients, self.runners = deque(maxlen=128), set(), {}
        self.processes = {}
        self.core_bin_dir = Path(args.core_bin_dir).absolute() if args.core_bin_dir else None
        self.store = dict(api_version=API_VERSION, tasks={}, profiles={}, operations={}, attempt_history={}, network_plans={}, network_bindings={}, task_inputs={}, summaries={})
        self.page_snapshots, self.page_cursors = {}, {}
        path = self.state_dir / "state.json"
        if path.exists():
            if path.is_symlink() or path.stat().st_uid != os.getuid(): raise RuntimeError("unsafe state file")
            if path.stat().st_size > 8 * MAX_FRAME: raise RuntimeError("state file exceeds limit")
            self.store = json.loads(path.read_text())
            if self.store.get("api_version") != API_VERSION:
                raise RuntimeError("state API mismatch; preserve the old state directory and use a new private directory")
            self.store.setdefault("task_inputs", {}); self.store.setdefault("summaries", {})
            self.store.setdefault("attempt_history", {})
            self.store.setdefault("network_plans", {})
            self.store.setdefault("network_bindings", {})
            for t in self.store["tasks"].values():
                t.setdefault("backend_evidence", empty_evidence())
                t.setdefault("effective_policy", self.pending_policy())
                t["attempt"].setdefault("quiescence", "not_applicable")
                if t["state"] in ACTIVE:
                    t.update(state="recoverable", revision=t["revision"]+1)
                    t["attempt"]["state"] = "recoverable"
                    t["attempt"]["quiescence"] = "confirmed"
                    t["metrics"].update(speed_bps=None, average_speed_bps=None, eta_seconds=None, channel_count_active=0, data_connections=0)
                    self.checkpoint(t)
                    if t["recovery"]["eligibility"] != "eligible":
                        t["state"] = "failed"; t["attempt"]["state"] = "failed"
                    t["transfer_status"] = "fail"
                    t["evidence_status"] = "incomplete"
        for transfer_id, task in self.store["tasks"].items():
            history = self.store["attempt_history"].setdefault(transfer_id, [])
            for i, previous in enumerate(history):
                if previous.get("attempt_id") == task["attempt"]["attempt_id"]:
                    history[i] = copy.deepcopy(task["attempt"]); break
            else:
                history.append(copy.deepcopy(task["attempt"]))
        self.save()
    def pending_policy(self):
        return dict(resolution="pending", mode="unknown", scheduler="unknown", range_policy="unknown", checksum="unknown", resume_enabled=None, control_tls="unknown", data_tls="unknown", channel_count=None, applied_at=None)
    def save(self):
        data = json.dumps(self.store, ensure_ascii=False, allow_nan=False)
        if len(data.encode()) > 8 * MAX_FRAME: raise RuntimeError("state budget exceeded")
        fd, name = tempfile.mkstemp(prefix="state-", dir=self.state_dir)
        try:
            with os.fdopen(fd, "w") as out:
                out.write(data); out.flush(); os.fsync(out.fileno())
            os.replace(name, self.state_dir / "state.json")
            fd = os.open(self.state_dir, os.O_DIRECTORY)
            try: os.fsync(fd)
            finally: os.close(fd)
        finally:
            if os.path.exists(name): os.unlink(name)
    def emit(self, t):
        t["attempt"].setdefault("quiescence", "not_applicable")
        history = self.store["attempt_history"].setdefault(t["transfer_id"], [])
        for i, previous in enumerate(history):
            if previous.get("attempt_id") == t["attempt"]["attempt_id"]:
                history[i] = copy.deepcopy(t["attempt"]); break
        else: history.append(copy.deepcopy(t["attempt"]))
        t["revision"] += 1
        self.sequence += 1
        self.save()
        event = dict(agent_epoch=self.epoch, sequence=self.sequence,
                     transfer_id=t["transfer_id"], attempt_id=t["attempt"]["attempt_id"],
                     revision=t["revision"], timestamp=now(), type="transfer.state", payload=copy.deepcopy(t))
        self.events.append(event)
        for client in list(self.clients): client.event(event)
    def snapshot(self, t): return dict(kind="transfer", snapshot=copy.deepcopy(t))
    def task(self, p):
        try: return self.store["tasks"][p["transfer_id"]]
        except KeyError: raise RpcError("not_found", "任务不存在")
    def ensure_demo(self):
        if not self.args.demo: raise RpcError("unsupported", "该操作仅在显式 --demo 开发诊断模式可用")

    def ensure_real(self):
        if self.args.demo: raise RpcError("unsupported", "开发诊断模式不执行真实文件传输")
        if self.core_bin_dir is None:
            raise RpcError("unsupported", "真实 adapter 未配置 CPNetFlux core；请设置 --core-bin-dir")
        if not self.core_bin_dir.is_dir():
            raise RpcError("unsupported", "CPNetFlux core 目录不存在或无权限")

    def core_binary(self, name):
        self.ensure_real()
        path = self.core_bin_dir / name
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RpcError("unsupported", "缺少真实后端可执行文件: " + name)
        return str(path)

    def profile_for(self, endpoint, profile=None):
        node_id = endpoint.get("node_id")
        profile = profile if profile is not None else self.store["profiles"].get(node_id)
        if profile is None:
            raise RpcError("not_found", "请先保存远端节点配置")
        if profile.get("auth_mode") == "anonymous":
            raise RpcError("auth_failed", "真实传输要求 TLS/认证；匿名模式不可用于真实任务")
        ca = profile.get("ca_path")
        if not ca or not Path(ca).is_file() or not os.access(ca, os.R_OK):
            raise RpcError("tls_identity_failed", "CA 文件不存在或不可读")
        if profile.get("auth_mode") == "token":
            ref = profile.get("credential_ref")
            if not ref or not ref.startswith("file:"):
                raise RpcError("keyring_locked", "当前版本需要受权限保护的 token 文件引用 file:<path>")
            token = Path(ref[5:])
            if not token.is_file() or not os.access(token, os.R_OK) or (token.stat().st_mode & 0o077):
                raise RpcError("permission_denied", "token 文件必须存在、可读且权限为 0600")
        else:
            raise RpcError("unsupported", "admin credential 的 OS secret-store 注入尚未实现")
        return profile

    def validate_real_request(self, p):
        self.ensure_real()
        local = p["source"] if p["direction"] == "upload" else p["destination"]
        remote = p["destination"] if p["direction"] == "upload" else p["source"]
        path = Path(local["path"])
        if not path.is_absolute() or not path.exists():
            raise RpcError("permission_denied", "本地路径必须存在且为绝对路径")
        if p["direction"] == "upload" and not os.access(path, os.R_OK):
            raise RpcError("permission_denied", "本地源路径不可读")
        if p["direction"] == "download" and not os.access(path.parent if path.suffix else path, os.W_OK):
            raise RpcError("permission_denied", "本地目标父目录不可写")
        if remote["path"].startswith("/") or ".." in Path(remote["path"]).parts:
            raise RpcError("permission_denied", "远端路径必须是 root-relative 且不能越界")
        frozen = self.store.get("task_inputs", {}).get(p.get("transfer_id"), {})
        profile = self.profile_for(remote, frozen.get("profile"))
        if remote.get("root_id") != profile.get("default_root_id"):
            raise RpcError("unsupported", "root_id 必须匹配节点默认授权 root；核心不支持运行时切换 root")
        binding = frozen.get("binding") if frozen else self.store.get("network_bindings", {}).get(remote["node_id"])
        if binding:
            verification = self._verify_binding(remote["node_id"], binding)
            if not verification["verified"]:
                raise RpcError("invalid_state", "已配置的本机网卡绑定当前不可用，请重新检测并应用")
        # The unified tree adapter is the only authenticated real endpoint currently
        # exposed by the core. Keep single-file requests explicit and actionable until
        # the file client gains the same control-plane contract.
        if path.is_file():
            raise RpcError("unsupported", "核心 file client 尚未提供统一 TLS/认证控制面；请使用目录任务，或先升级 core adapter")
        return path, remote, profile

    def auth_probe_command(self, profile):
        return [
            self.core_binary("cpnetflux-auth-probe"),
            "--auth-probe",
            "--host", str(profile["host"]),
            "--port", str(profile["control_port"]),
            "--auth-mode", "token",
            "--auth-token-file", profile["credential_ref"][5:],
            "--tls-mode", "required",
            "--tls-server-name", profile.get("server_name") or profile["host"],
            "--tls-ca-file", profile["ca_path"],
            "--data-tls-mode", "required",
        ]

    def run_auth_probe(self, profile):
        try:
            completed = subprocess.run(
                self.auth_probe_command(profile),
                capture_output=True,
                text=True,
                timeout=15,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return None, "认证检查超时（15秒），请检查服务端和防火墙"
        except OSError as exc:
            return None, "无法启动认证检查: " + str(exc)
        payload = None
        for line in reversed(completed.stdout.splitlines()):
            try:
                candidate = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(candidate, dict) and "authentication" in candidate:
                payload = candidate
                break
        if payload is None:
            return None, completed.stderr.strip() or "认证检查未返回结构化结果"
        if completed.returncode != 0 and payload.get("authentication") == "authenticated":
            return None, "认证检查退出状态与结果不一致"
        return payload, payload.get("error") or ""

    def build_real_command(self, t):
        path = Path(t["source"]["path"] if t["direction"] == "upload" else t["destination"]["path"])
        remote = t["destination"] if t["direction"] == "upload" else t["source"]
        frozen = self.store.get("task_inputs", {}).get(t.get("transfer_id"), {})
        profile = self.profile_for(remote, frozen.get("profile"))
        binding = frozen.get("binding") if frozen else self.store.get("network_bindings", {}).get(remote["node_id"])
        options = self.resolve_options(t["policy"], binding)
        args = ["--host", profile["host"], "--port", str(profile["control_port"]),
                "--connections", str(options["channel_count"]), "--checksum", options["checksum"],
                "--tls-mode", "required", "--tls-server-name", profile.get("server_name") or profile["host"],
                "--tls-ca-file", profile["ca_path"],
                "--data-tls-mode", t["policy"]["data_tls"], "--event-log", t["backend_evidence"]["event_log"],
                "--auth-mode", "token", "--auth-token-file", profile["credential_ref"][5:],
                "--json-summary", t["backend_evidence"]["summary"], "--phase-timing", "on"]
        for key, flag in (("channel_count", "file-parallelism"), ("channel_count", "link-set-links"),
                          ("pending_window", "data-pending-window"), ("file_scheduling", "file-scheduling"),
                          ("reconnect_attempts", "reconnect-attempts"), ("compression", "compression"),
                          ("chunk_size", "chunk-size"), ("buffer_size", "buffer-size"),
                          ("control_pipeline_depth", "control-pipeline-depth"),
                          ("scheduler_mode", "scheduler"), ("scheduler_policy", "scheduler-policy"),
                          ("scheduler_work_item_min_bytes", "scheduler-workitem-min-bytes"),
                          ("scheduler_work_item_max_bytes", "scheduler-workitem-max-bytes"),
                          ("scheduler_default_rtt_ms", "scheduler-default-rtt-ms"),
                          ("scheduler_capacity_gbps", "scheduler-capacity-gbps")):
            args += ["--" + flag, str(options[key])]
        if options["transfer_timeout_ms"]:
            args += ["--transfer-timeout-ms", str(options["transfer_timeout_ms"])]
        if t["attempt"]["attempt_number"] > 1 or t["policy"]["resume_policy"] == "require":
            args += ["--resume"]
        if binding:
            addresses = binding.get("local_bind_addresses", [])
            if addresses:
                args += ["--local-bind-addresses", ",".join(addresses)]
            peers = self.binding_peers(binding)
            if peers:
                args += ["--peer-addresses", ",".join(peers)]
        if t["direction"] == "upload":
            exe = self.core_binary("cpnetflux-tree-upload-client")
            args += ["--source-dir", str(path), "--dest-dir", remote["path"]]
        else:
            exe = self.core_binary("cpnetflux-tree-download-client")
            args += ["--source-dir", remote["path"], "--dest-dir", str(path)]
        return [exe] + args

    def apply_real_policy(self, t, summary):
        """Resolve policy only from a successful, verified V3 core summary."""
        t.update(actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown")
        t["effective_policy"] = self.pending_policy()
        lanes = summary.get("link_set_accepted_links")
        if (summary.get("result") != "pass" or summary.get("actual_transport") != "v3"
                or not summary.get("control_tls_verified") or not isinstance(summary.get("data_tls_verified"), bool)
                or not isinstance(lanes, int) or not 1 <= lanes <= 10):
            return
        observed_scheduler = {"dynamic": "dynamic_ring", "static": "static"}.get(
            summary.get("actual_file_scheduling"), "unknown")
        observed_checksum = {"crc32c": "crc32c", "none": "none"}.get(
            summary.get("checksum_algorithm"), "unknown")
        if observed_scheduler == "unknown" or observed_checksum == "unknown": return
        t["actual_mode"] = "v3"
        t["actual_scheduler"] = observed_scheduler
        t["actual_range_policy"] = "auto"
        t["effective_policy"] = dict(
            resolution="resolved", mode="v3", scheduler=observed_scheduler,
            range_policy=t["actual_range_policy"], checksum=observed_checksum,
            resume_enabled=summary.get("resume") if isinstance(summary.get("resume"), bool) else None,
            control_tls="verified", data_tls="verified" if summary["data_tls_verified"] else "off",
            channel_count=lanes, applied_at=now())

    def read_real_evidence(self, t, summary):
        local = Path(t["source"]["path"] if t["direction"] == "upload" else t["destination"]["path"])
        m = t["metrics"]
        if summary:
            total = summary.get("bytes_total")
            moved = summary.get("bytes_transferred")
            files = summary.get("file_count")
            m.update(files_total=files, files_committed=summary.get("completed_files"),
                     files_failed=summary.get("failed_files"), files_skipped=summary.get("skipped_files"),
                     bytes_total=total, bytes_transferred=moved, bytes_verified=None,
                     bytes_committed=total if summary.get("result") == "pass" else None, channel_count_active=0, queue_depth_current=0,
                     data_connections=summary.get("data_connect_count"), speed_bps=None,
                     average_speed_bps=None, eta_seconds=None)
            t["scan_complete"] = isinstance(files, int) and isinstance(total, int)
            t["backend_evidence"]["phase_timing"].update(
                control_prepare=summary.get("control_prepare_seconds"),
                payload_io=summary.get("elapsed_seconds"),
                transfer_complete_wait=summary.get("transfer_complete_wait_seconds"))
            t["backend_evidence"]["checksum"] = {"algorithm": summary.get("checksum_algorithm", "unknown"),
                "status": "not_requested" if summary.get("checksum_algorithm") == "none" else ("pass" if summary.get("result") == "pass" else "fail")}
            suffix = ".cpnetflux.tree.upload.manifest" if t["direction"] == "upload" else ".cpnetflux.tree.download.manifest"
            manifest_path = Path(str(local) + suffix)
            t["backend_evidence"]["manifest"] = {"path": str(manifest_path),
                                                  "status": "observed" if manifest_path.exists() else "unknown"}
        elif local.exists() and local.is_dir():
            files = [f for f in local.rglob("*") if f.is_file()]
            m.update(files_total=len(files), files_committed=None, files_failed=None, files_skipped=None,
                     bytes_total=sum(f.stat().st_size for f in files), bytes_transferred=None,
                     bytes_verified=None, bytes_committed=None, channel_count_active=0,
                     queue_depth_current=None, data_connections=None, speed_bps=None,
                     average_speed_bps=None, eta_seconds=None)
    def page(self, kind, t, p, items, query, extra=None):
        now_mono = asyncio.get_running_loop().time()
        for token, cursor in list(self.page_cursors.items()):
            if cursor["expires"] <= now_mono:
                self.page_cursors.pop(token, None)
        for snapshot_id, snapshot in list(self.page_snapshots.items()):
            if snapshot["expires"] <= now_mono:
                self.page_snapshots.pop(snapshot_id, None)
        if p["cursor"] is None:
            while len(self.page_snapshots) >= MAX_PAGE_SNAPSHOTS:
                expired_id, _ = self.page_snapshots.popitem()
                for token, cursor in list(self.page_cursors.items()):
                    if cursor["snapshot_id"] == expired_id: self.page_cursors.pop(token, None)
            snapshot_id = uid()
            snapshot = dict(snapshot_id=snapshot_id, query=query, transfer_id=t["transfer_id"],
                            attempt_id=p.get("attempt_id"), agent_epoch=self.epoch,
                            revision=t["revision"], snapshot_sequence=self.sequence,
                            items=copy.deepcopy(items), extra=copy.deepcopy(extra or {}), expires=now_mono+PAGE_TTL_SECONDS)
            self.page_snapshots[snapshot_id] = snapshot
            offset = 0
        else:
            cursor = self.page_cursors.get(p["cursor"])
            if not cursor or cursor["expires"] <= now_mono:
                self.page_cursors.pop(p["cursor"], None)
                raise RpcError("cursor_expired", "实体快照游标无效或已过期")
            snapshot_id = cursor["snapshot_id"]
            snapshot = self.page_snapshots.get(snapshot_id)
            if not snapshot or snapshot["query"] != query:
                raise RpcError("cursor_expired", "游标与查询身份或快照不匹配")
            offset = cursor["offset"]
        limit=p["limit"]
        page_items=snapshot["items"][offset:offset+limit]
        next_cursor=None
        if offset+limit < len(snapshot["items"]):
            next_cursor=uid()
            while len(self.page_cursors) >= MAX_PAGE_CURSORS:
                self.page_cursors.pop(next(iter(self.page_cursors)))
            self.page_cursors[next_cursor]=dict(snapshot_id=snapshot_id,offset=offset+limit,expires=snapshot["expires"])
        result=dict(kind=kind,transfer_id=snapshot["transfer_id"],attempt_id=snapshot["attempt_id"],
                    snapshot_id=snapshot_id,agent_epoch=snapshot["agent_epoch"],revision=snapshot["revision"],
                    snapshot_sequence=snapshot["snapshot_sequence"],items=page_items,next_cursor=next_cursor)
        result.update(snapshot.get("extra",{}))
        return result
    def attempts_for(self, transfer_id):
        history=copy.deepcopy(self.store["attempt_history"].get(transfer_id, []))
        task=self.store["tasks"][transfer_id]
        if not any(a.get("attempt_id")==task["attempt"]["attempt_id"] for a in history): history.append(copy.deepcopy(task["attempt"]))
        return history
    def demo_files(self, t, attempt_id):
        if attempt_id not in {a["attempt_id"] for a in self.attempts_for(t["transfer_id"])}:
            raise RpcError("not_found", "attempt 不属于 transfer")
        m=t["metrics"]; moved=m.get("bytes_transferred") or 0; verified=m.get("bytes_verified") or 0; committed=m.get("bytes_committed") or 0
        total=4*1024*1024
        moved=max(0,min(total,moved)); verified=max(0,min(total,verified)); committed=max(0,min(total,committed))
        result=[]
        for i in range(4):
            size=1024*1024; start=i*size; n=max(0,min(size,moved-start)); v=max(0,min(size,verified-start)); done=committed>=start+size
            state="committed" if done else ("transferring" if n else ("queued" if t["state"] in {"queued","scanning","connecting","negotiating"} else "recoverable"))
            check=dict(algorithm="crc32c" if t["policy"]["checksum_policy"]=="required_crc32c" else "none",expected="simulated" if v==size else None,actual="simulated" if v==size else None,status=("pass" if v==size else ("not_requested" if t["policy"]["checksum_policy"]=="none" else "pending")))
            result.append(dict(transfer_id=t["transfer_id"],attempt_id=attempt_id,file_id=f"demo-file-{i+1}",generation=1,relative_path=f"demo/file-{i+1}.bin",size=size,mtime_ns=0,state=state,bytes_transferred=n,bytes_verified=v,range_count=1,error=None,checksum=check))
        return result
    def demo_ranges(self, t, attempt_id, file_id, generation):
        files=self.demo_files(t,attempt_id)
        file=next((f for f in files if f["file_id"]==file_id and f["generation"]==generation),None)
        if file is None: raise RpcError("not_found", "file identity 不属于该 attempt")
        n=file["bytes_transferred"] or 0
        if n==0: return []
        return [dict(transfer_id=t["transfer_id"],attempt_id=attempt_id,file_id=file_id,range_id="demo-range-1",generation=generation,offset=0,length=file["size"],state="verified" if file["bytes_verified"]==file["size"] else "transferring",bytes_transferred=n,error=None,attempt_number=next(a["attempt_number"] for a in self.attempts_for(t["transfer_id"]) if a["attempt_id"]==attempt_id),checksum=copy.deepcopy(file["checksum"]))]
    def _network_inventory(self, node_id):
        if self.args.demo:
            interfaces = [{
                "interface_id": "demo:eth0", "name": "eth0", "kind": "physical",
                "state": "up", "carrier": True, "addresses": ["192.0.2.10"],
                "mtu": 1500, "speed_bps": 1000000000, "management": True
            }]
            management_id = "demo:eth0"
        else:
            addresses_by_name = {}
            try:
                completed = subprocess.run(
                    ["ip", "-j", "address", "show"],
                    capture_output=True, text=True, timeout=3, check=False)
                if completed.returncode == 0:
                    for item in json.loads(completed.stdout):
                        values = []
                        for address in item.get("addr_info", []):
                            local = address.get("local")
                            if isinstance(local, str) and local:
                                values.append(local)
                        addresses_by_name[item.get("ifname", "")] = values
            except (OSError, ValueError, json.JSONDecodeError, subprocess.TimeoutExpired):
                addresses_by_name = {}
            interfaces = []
            for _, name in socket.if_nameindex():
                base = Path("/sys/class/net") / name
                def read_text(filename, default):
                    try:
                        return (base / filename).read_text().strip()
                    except (OSError, UnicodeDecodeError):
                        return default
                state_text = read_text("operstate", "unknown")
                state = state_text if state_text in {"up", "down"} else "unknown"
                carrier_text = read_text("carrier", "0")
                carrier = carrier_text == "1"
                kind = "loopback" if name == "lo" else (
                    "physical" if (base / "device").exists() else "virtual")
                try:
                    mtu = int(read_text("mtu", "0"))
                except ValueError:
                    mtu = 0
                speed = None
                try:
                    value = int(read_text("speed", "-1"))
                    if value >= 0:
                        speed = value * 1000000
                except ValueError:
                    pass
                interfaces.append({
                    "interface_id": "linux:" + name, "name": name, "kind": kind,
                    "state": state, "carrier": carrier,
                    "addresses": sorted(set(addresses_by_name.get(name, []))),
                    "mtu": mtu, "speed_bps": speed, "management": False
                })
            management_name = None
            try:
                for line in Path("/proc/net/route").read_text().splitlines()[1:]:
                    fields = line.split()
                    if len(fields) > 1 and fields[1] == "00000000":
                        management_name = fields[0]
                        break
            except (OSError, UnicodeDecodeError):
                pass
            management_id = "linux:" + management_name if management_name else None
            for item in interfaces:
                item["management"] = item["interface_id"] == management_id
        canonical = dict(node_id=node_id, management_interface_id=management_id,
                         interfaces=interfaces)
        revision = hashlib.sha256(json.dumps(
            canonical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:24]
        warnings = []
        usable = [x for x in interfaces
                  if x["kind"] != "loopback" and x["state"] == "up"
                  and x["carrier"] and x["addresses"]]
        if not usable:
            warnings.append("没有检测到带地址且链路为 up 的可用网卡")
        elif len(usable) == 1:
            warnings.append("当前只有一块可用网卡，无法形成多条独立链路")
        if management_id is None:
            warnings.append("未识别默认管理路由；应用绑定不会修改系统路由")
        return dict(kind="network_inventory", node_id=node_id,
                    inventory_revision=revision, observed_at=now(),
                    interfaces=interfaces, management_interface_id=management_id,
                    warnings=warnings)

    def _network_plan(self, node_id, inventory_revision, requested_links, peer_addresses, selections=None):
        inventory = self._network_inventory(node_id)
        if inventory["inventory_revision"] != inventory_revision:
            raise RpcError("revision_conflict", "网卡清单已变化，请重新检测")
        explicit_peers = any(address is not None for address in peer_addresses)
        if len(peer_addresses) > requested_links or (explicit_peers and (
                len(peer_addresses) != requested_links or None in peer_addresses)):
            raise RpcError("invalid_state", "远端映射必须为每条链路指定地址，不能混用空值")
        if explicit_peers:
            try:
                peers = [ipaddress.ip_address(x) for x in peer_addresses]
                if any(x.is_unspecified or x.is_multicast or x.is_link_local or x.is_loopback for x in peers):
                    raise ValueError()
            except ValueError:
                raise RpcError("invalid_state", "远端映射必须为可用的数字 IPv4/IPv6 地址")
            peer_addresses = [str(x) for x in peers]
        host = self.store.get("profiles", {}).get(node_id, {}).get("host")
        try:
            peer_family = ipaddress.ip_address(host).version if host else None
        except ValueError:
            peer_family = None
        candidates = []
        for interface in inventory["interfaces"]:
            if interface["kind"] == "loopback" or interface["state"] != "up" or not interface["carrier"]:
                continue
            usable = []
            for text in interface["addresses"]:
                try:
                    address = ipaddress.ip_address(text)
                except ValueError:
                    continue
                if address.is_loopback or address.is_link_local or address.is_unspecified or address.is_multicast:
                    continue
                usable.append(address)
            if usable:
                candidates.append(dict(interface, addresses=[str(x) for x in usable]))
        if selections is not None:
            if len(selections) != requested_links or len({x["interface_id"] for x in selections}) != len(selections):
                raise RpcError("invalid_state", "手动选择数量必须匹配链路数，且每块网卡只能选择一次")
            selected_interfaces = []
            for selection in selections:
                interface = next((x for x in inventory["interfaces"] if x["interface_id"] == selection["interface_id"]), None)
                try: address = ipaddress.ip_address(selection["local_address"])
                except ValueError: raise RpcError("invalid_state", "绑定地址必须是数字 IP")
                if (interface is None or interface["kind"] == "loopback" or interface["state"] != "up"
                        or not interface["carrier"] or str(address) not in interface["addresses"]
                        or address.is_loopback or address.is_link_local or address.is_unspecified
                        or address.is_multicast):
                    raise RpcError("invalid_state", "选择的本机地址不可用或与远端地址族不匹配")
                selected_interfaces.append(dict(interface, addresses=[str(address)]))
            candidates = selected_interfaces
        bindings = []
        for lane, interface in enumerate(candidates[:requested_links]):
            peers = peer_addresses[lane] if lane < len(peer_addresses) else None
            family = ipaddress.ip_address(peers).version if peers else peer_family
            if selections is None:
                # Match each lane's family before choosing an unused local interface.
                used = {x["interface_id"] for x in bindings}
                interface = next((x for x in candidates if x["interface_id"] not in used and
                    any(family is None or ipaddress.ip_address(a).version == family for a in x["addresses"])), None)
                if interface is None:
                    break
                addresses = [ipaddress.ip_address(x) for x in interface["addresses"]
                    if family is None or ipaddress.ip_address(x).version == family]
                local_address = str(min(addresses, key=lambda a: (a.version, int(a))))
            else:
                local_address = interface["addresses"][0]
                if family and ipaddress.ip_address(local_address).version != family:
                    raise RpcError("invalid_state", "选择的本机地址与该链路远端地址族不匹配")
            bindings.append(dict(lane_id=lane, interface_id=interface["interface_id"],
                                 local_address=local_address,
                                 peer_address=peers))
        warnings = list(inventory["warnings"])
        if len(bindings) < requested_links:
            warnings.append("可用网卡数量少于请求链路数量，请降低链路数后重新生成")
        payload = dict(node_id=node_id, inventory_revision=inventory_revision,
                       requested_links=requested_links, bindings=bindings)
        plan_id = "plan-" + hashlib.sha256(json.dumps(
            payload, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:24]
        result = dict(kind="network_plan", plan_id=plan_id, node_id=node_id,
                      inventory_revision=inventory_revision,
                      requested_links=requested_links, bindings=bindings,
                      warnings=warnings, requires_privilege=False)
        if len(self.store["network_plans"]) >= 100 and plan_id not in self.store["network_plans"]:
            raise RpcError("invalid_state", "网卡方案预算已满，请复用已有方案")
        self.store["network_plans"][plan_id] = copy.deepcopy(result)
        self.save()
        return result

    def _network_verify(self, node_id, plan_id):
        plan = self.store.get("network_plans", {}).get(plan_id)
        if plan is None or plan.get("node_id") != node_id:
            raise RpcError("not_found", "网卡绑定方案不存在")
        binding = self.store.get("network_bindings", {}).get(node_id)
        if binding is None or binding.get("plan_id") != plan_id:
            raise RpcError("invalid_state", "网卡绑定方案尚未应用")
        return self._verify_binding(node_id, binding)

    def _verify_binding(self, node_id, binding):
        plan_id = binding["plan_id"]
        inventory = self._network_inventory(node_id)
        by_id = {x["interface_id"]: x for x in inventory["interfaces"]}
        verified = True
        warnings = list(inventory["warnings"])
        effective = []
        for item in binding.get("bindings", []):
            interface = by_id.get(item["interface_id"])
            ok = interface is not None and interface["state"] == "up" and interface["carrier"] \
                and item["local_address"] in interface["addresses"]
            if not ok:
                verified = False
                warnings.append("绑定地址或网卡状态已变化: " + item["interface_id"])
            effective.append(copy.deepcopy(item))
        return dict(kind="network_verify", node_id=node_id, plan_id=plan_id,
                    inventory_revision=inventory["inventory_revision"], verified=verified,
                    physical_path_verified=False, effective_bindings=effective,
                    warnings=warnings)

    @staticmethod
    def binding_peers(binding):
        rows = (binding or {}).get("bindings", [])
        peers = [x.get("peer_address") for x in rows]
        if not any(x is not None for x in peers): return []
        try:
            if any(x is None for x in peers): raise ValueError()
            return [str(ipaddress.ip_address(x)) for x in peers]
        except ValueError:
            raise RpcError("invalid_state", "保存的远端映射不完整或不是数字 IP，请重新生成方案")

    def resolve_options(self, policy, binding=None):
        p = dict(self.policy_defaults(), **policy)
        if (p["requested_mode"] != "v3" or p["fallback_policy"] != "reject"
                or p["range_policy"] != "auto" or p["queue_depth"] != "auto"
                or p["conflict_policy"] != "fail"):
            raise RpcError("unsupported", "CPNetFlux 只运行 V3；队列和 range 由核心自动管理，冲突策略固定为失败")
        addresses = (binding or {}).get("local_bind_addresses", [])
        channels = (len(addresses) or 1) if p["channel_count"] == "auto" else p["channel_count"]
        if addresses and len(addresses) not in {1, channels}:
            raise RpcError("invalid_state", "通道数须匹配绑定地址数；单个地址可用于所有通道")
        peers = self.binding_peers(binding)
        if peers and (len(peers) not in {1, channels} or len(peers) != len(addresses)):
            raise RpcError("invalid_state", "远端映射数量须匹配绑定地址和通道数")
        scheduler = "dynamic_ring" if p["scheduler"] == "auto" else p["scheduler"]
        if scheduler == "static" and (p["control_pipeline_depth"] or p["scheduler_mode"] == "global"):
            raise RpcError("unsupported", "控制流水线和 global 调度要求 dynamic 文件调度")
        if p["reconnect_attempts"] and (p["resume_policy"] == "never" or p["checksum_policy"] == "none"):
            raise RpcError("unsupported", "自动重连需要允许 resume 和 CRC32C 校验")
        if p["scheduler_policy"] == "adaptive" and p["scheduler_mode"] != "global":
            raise RpcError("unsupported", "adaptive 调度策略要求启用 global 调度")
        if p["scheduler_work_item_min_bytes"] > p["scheduler_work_item_max_bytes"]:
            raise RpcError("invalid_state", "调度最小工作块不能超过最大工作块")
        return dict(channel_count=channels, pending_window=p["control_pipeline_depth"] or p["pending_window"],
            queue_capacity=channels*(p["control_pipeline_depth"] or p["pending_window"])*2 if scheduler=="dynamic_ring" else 0,
            checksum="crc32c" if p["checksum_policy"] == "required_crc32c" else "none",
            file_scheduling="dynamic" if scheduler == "dynamic_ring" else "static",
            reconnect_attempts=p["reconnect_attempts"],
            transfer_timeout_ms=p["transfer_timeout_ms"] or (300000 if p["reconnect_attempts"] else 0),
            compression=p["compression"], chunk_size=p["chunk_size"], buffer_size=p["buffer_size"],
            control_pipeline_depth=p["control_pipeline_depth"],
            scheduler_mode=p["scheduler_mode"], scheduler_policy=p["scheduler_policy"],
            scheduler_work_item_min_bytes=p["scheduler_work_item_min_bytes"],
            scheduler_work_item_max_bytes=p["scheduler_work_item_max_bytes"],
            scheduler_default_rtt_ms=p["scheduler_default_rtt_ms"],
            scheduler_capacity_gbps=p["scheduler_capacity_gbps"])

    @staticmethod
    def policy_defaults():
        return dict(requested_mode="v3",scheduler="dynamic_ring",channel_count="auto",
            pending_window=2,queue_depth="auto",range_policy="auto",checksum_policy="required_crc32c",
            resume_policy="allow",control_tls="verify_ca",data_tls="required",fallback_policy="reject",
            conflict_policy="fail",experimental_raw_ack=False,reconnect_attempts=0,
            transfer_timeout_ms=0,compression="off",chunk_size=1048576,buffer_size=65536,
            control_pipeline_depth=0,scheduler_mode="off",scheduler_policy="fixed",
            scheduler_work_item_min_bytes=67108864,scheduler_work_item_max_bytes=268435456,
            scheduler_default_rtt_ms=10,scheduler_capacity_gbps=0.01)

    def contract(self):
        unsupported={"transfer.pause":"核心尚未提供暂停/静止屏障",
                     "directory.list":"核心尚未提供安全的结构化目录查询"}
        methods=[]
        for req in self.schema["$defs"]["request"]["oneOf"]:
            method=req["properties"]["method"]["const"]
            reason=unsupported.get(method)
            if not self.args.demo and method in {"transfer.validate","transfer.create","transfer.start","transfer.resume","node.testConnection"}:
                try:
                    for name in ("cpnetflux-tree-upload-client","cpnetflux-tree-download-client","cpnetflux-auth-probe"): self.core_binary(name)
                except RpcError as exc: reason=exc.message
            support="unsupported" if reason else ("simulated" if self.args.demo else "supported")
            methods.append(dict(method=method,support=support,reason=reason))
        return dict(kind="agent_contract",api_version=API_VERSION,runtime_protocol="v3",
            adapter="demo" if self.args.demo else "real",methods=methods,
            policy_defaults=self.policy_defaults(),policy_schema=copy.deepcopy(self.schema["$defs"]["policy"]),
            limitations=["只提供带 TLS/token 的目录任务，不提供统一认证的单文件传输",
                "真实文件/range 明细在传输结束后由核心摘要生成，没有实时逐块速率",
                "网卡配置只写 CPNetFlux 本机绑定和逐链路远端 IP，不改系统路由；映射须指向同一 V3 服务和根目录",
                "local_supported 表示本机核心支持项；远端 peer 能力仍需连接实际协商"])

    def validation(self, p):
        if self.args.demo:
            remote=p["destination"] if p["direction"]=="upload" else p["source"]
            binding=self.store["network_bindings"].get(remote["node_id"])
        else:
            self.validate_real_request(p)
            remote=p["destination"] if p["direction"]=="upload" else p["source"]
            binding=self.store["network_bindings"].get(remote["node_id"])
        options=self.resolve_options(p["policy"],binding)
        if not self.args.demo and p["policy"]["resume_policy"]=="require":
            local=p["source"] if p["direction"]=="upload" else p["destination"]
            suffix=".cpnetflux.tree."+p["direction"]+".manifest"
            if not Path(local["path"]+suffix).is_file():
                raise RpcError("checkpoint_missing","require resume 需要已有 V3 tree manifest；具体内容由核心检查")
        warnings=["preview 不连接远端；认证、数据端口和远端目录由启动后的核心验证"]
        if p["policy"].get("control_pipeline_depth",0) and p["policy"]["control_pipeline_depth"] != p["policy"]["pending_window"]:
            warnings.append("V3 共享流水线使用深度 %s 作为实际窗口；请求 pending_window=%s，实际=%s" %
                (p["policy"]["control_pipeline_depth"],p["policy"]["pending_window"],options["pending_window"]))
        return dict(kind="transfer_validation",valid=True,runtime_protocol="v3",
            resolved_options=options,binding_plan_id=binding["plan_id"] if binding else None,
            warnings=warnings)

    def summary_for(self, t, attempt_id):
        if not any(x["attempt_id"]==attempt_id for x in self.attempts_for(t["transfer_id"])):
            raise RpcError("not_found","attempt 不属于该任务")
        record=self.store.get("summaries",{}).get(attempt_id)
        if record is None: return None
        return self.load_summary(record)

    def load_summary(self, path):
        if not path or not Path(path).is_file(): return None
        try:
            with Path(path).open() as stream: value=json.load(stream)
            if not isinstance(value,dict): return None
            return value
        except (OSError,ValueError): return None

    def real_files(self, t, attempt_id):
        summary=self.summary_for(t,attempt_id)
        if summary is None: return []
        groups={}
        for row in summary.get("file_timings",[]): groups.setdefault((row["file_id"],row["generation"]),[]).append(row)
        success=summary.get("result")=="pass"
        return [dict(transfer_id=t["transfer_id"],attempt_id=attempt_id,file_id=str(key[0]),
            generation=key[1],relative_path=rows[0]["relative_path"],size=rows[0]["size"],mtime_ns=None,
            state="committed" if success else "recoverable",bytes_transferred=None,bytes_verified=None,
            range_count=sum(1 for row in rows if row["length"]>0),error=None,checksum=dict(algorithm=summary.get("checksum_algorithm","unknown"),
                expected=None,actual=None,status="not_requested" if summary.get("checksum_algorithm")=="none" else "pass" if success else "pending"))
            for key,rows in sorted(groups.items())]

    def real_ranges(self, t, p):
        summary=self.summary_for(t,p["attempt_id"])
        rows=(summary or {}).get("file_timings",[])
        selected=[x for x in rows if str(x["file_id"])==p["file_id"] and x["generation"]==p["generation"]]
        if not selected: raise RpcError("not_found","最终摘要尚无该文件/range 观测证据")
        return [dict(transfer_id=t["transfer_id"],attempt_id=p["attempt_id"],file_id=p["file_id"],
            generation=p["generation"],range_id=str(x["range_id"]),offset=x["offset"],length=x["length"],
            state="verified" if summary["result"]=="pass" else "recoverable",bytes_transferred=None,
            attempt_number=next(a["attempt_number"] for a in self.attempts_for(t["transfer_id"]) if a["attempt_id"]==p["attempt_id"]),
            core_attempt=x["attempt"],error=None,checksum=dict(algorithm=summary["checksum_algorithm"],
            expected=None,actual=None,status="not_requested" if summary["checksum_algorithm"]=="none" else "pass" if summary["result"]=="pass" else "pending")) for x in selected if x["length"]>0]

    def completion_matches(self, t, summary):
        remote=t["destination"] if t["direction"]=="upload" else t["source"]
        frozen=self.store.get("task_inputs",{}).get(t["transfer_id"],{})
        binding=frozen.get("binding") if frozen else self.store["network_bindings"].get(remote["node_id"])
        options=self.resolve_options(t["policy"],binding); m=t["metrics"]
        peers=self.binding_peers(binding)
        endpoints=summary.get("lane_endpoints",[])
        if peers:
            if summary.get("peer_addresses") != peers or len(endpoints)!=options["channel_count"]:
                return False
            if {x.get("channel_index") for x in endpoints}!=set(range(options["channel_count"])):
                return False
            for endpoint in endpoints:
                lane=endpoint["channel_index"]
                try:
                    expected=ipaddress.ip_address(peers[lane%len(peers)])
                    local=ipaddress.ip_address(binding["local_bind_addresses"][lane%len(binding["local_bind_addresses"])])
                    if endpoint.get("lane_index") != lane or any(
                            ipaddress.ip_address(endpoint.get(key,""))!=expected
                            for key in ("control_peer_address","data_peer_address")) or any(
                            ipaddress.ip_address(endpoint.get(key,""))!=local
                            for key in ("control_local_address","data_local_address")):
                        return False
                except ValueError:
                    return False
        return (t["effective_policy"]["resolution"]=="resolved"
            and t["effective_policy"]["channel_count"]==options["channel_count"]
            and t["effective_policy"]["checksum"]==options["checksum"]
            and summary.get("resume")== (t["attempt"]["attempt_number"]>1 or t["policy"]["resume_policy"]=="require")
            and summary.get("reconnect_attempt_budget")==options["reconnect_attempts"]
            and isinstance(summary.get("recovery_attempts_used"),int) and 0<=summary["recovery_attempts_used"]<=options["reconnect_attempts"]
            and summary.get("actual_transport")=="v3"
            and summary.get("actual_file_scheduling")==options["file_scheduling"]
            and summary.get("data_pending_window")==options["pending_window"]
            and summary.get("compression_policy")==options["compression"]
            and summary.get("scheduler_mode")==options["scheduler_mode"]
            and summary.get("scheduler_policy")==options["scheduler_policy"]
            and summary.get("data_tls_verified")== (t["policy"]["data_tls"]=="required")
            and summary.get("control_pipeline_depth")==options["control_pipeline_depth"]
            and summary.get("queue_capacity")==options["queue_capacity"]
            and all(isinstance(m.get(k),int) and m[k]>=0 for k in ["files_total","files_committed","files_skipped","files_failed","bytes_total","bytes_committed"])
            and m["files_committed"]+m["files_skipped"]==m["files_total"]
            and m["files_failed"]==0 and m["bytes_total"] is not None and m["bytes_committed"]==m["bytes_total"])

    def dispatch(self, method, p, client):
        if method=="agent.getContract": return self.contract()
        if method=="transfer.validate": return self.validation(p)
        if method=="node.network.get":
            return dict(kind="network_configuration",node_id=p["node_id"],binding=copy.deepcopy(self.store["network_bindings"].get(p["node_id"])))
        if method=="transfer.evidence":
            t=self.task(p); summary=self.summary_for(t,p["attempt_id"])
            fields=self.schema["$defs"]["coreSummary"]["properties"]
            return dict(kind="transfer_evidence",transfer_id=t["transfer_id"],attempt_id=p["attempt_id"],
                observation="final_summary" if summary else "not_available",physical_path_verified=False,
                summary={k:copy.deepcopy(v) for k,v in summary.items() if k in fields} if summary else None)
        if method == "node.network.inventory":
            if not self.args.demo and p["node_id"] != "local" and p["node_id"] not in self.store["profiles"]:
                raise RpcError("not_found", "节点不存在")
            return self._network_inventory(p["node_id"])
        if method == "node.network.plan":
            if not self.args.demo and p["node_id"] != "local" and p["node_id"] not in self.store["profiles"]:
                raise RpcError("not_found", "节点不存在")
            return self._network_plan(p["node_id"], p["inventory_revision"],
                                      p["requested_links"], p["peer_addresses"], p.get("selections"))
        if method == "node.network.verify":
            return self._network_verify(p["node_id"], p["plan_id"])
        if method == "agent.getInfo":
            real_ready = (self.core_bin_dir is not None and self.core_bin_dir.is_dir() and
                          all((self.core_bin_dir / name).is_file() and os.access(self.core_bin_dir / name, os.X_OK)
                              for name in ("cpnetflux-tree-upload-client", "cpnetflux-tree-download-client", "cpnetflux-auth-probe")))
            return dict(kind="agent_info",agent_version="0.3.0",api_version=API_VERSION,agent_epoch=self.epoch,
                        adapter="demo" if self.args.demo else "real",max_frame_bytes=MAX_FRAME,
                        max_concurrent_requests=32,state_store="persistent",
                        features=["profiles","contract","network_inventory","network_plan","network_apply","network_verify","network_get","network_clear","network_peer_mapping"]+
                            (["transfer_lifecycle","entity_pages","cancel","resume","transfer_validate","transfer_evidence","v3"] if self.args.demo or real_ready else []))
        if method == "transfer.pause": raise RpcError("unsupported", "暂停尚未实现，任务状态保持不变")
        if method == "directory.list": raise RpcError("unsupported", "真实远端目录浏览尚未接入")
        if method == "node.testConnection":
            if self.args.demo:
                checks = {k:dict(state="unsupported", message="开发诊断模式不连接远端", error=None)
                          for k in ["dns_control_port", "tls_identity", "login", "capabilities", "directory_permission", "data_port"]}
                return dict(kind="connection_test", node_id=p["node_id"], adapter="demo", ready=False,
                            authentication="unknown", checks=checks)
            try:
                profile = self.profile_for({"node_id": p["node_id"]})
            except RpcError as exc:
                failed = dict(state="failed", message=exc.message, error=detail(exc.code, exc.message))
                state = "keyring_locked" if exc.code == "keyring_locked" else "failed"
                checks = {
                    "dns_control_port": dict(state="pending", message="未执行", error=None),
                    "tls_identity": failed if exc.code == "tls_identity_failed" else dict(state="pending", message="未执行", error=None),
                    "login": failed if exc.code in {"keyring_locked", "permission_denied", "unsupported"} else dict(state="pending", message="未执行", error=None),
                    "capabilities": dict(state="pending", message="未执行", error=None),
                    "directory_permission": dict(state="unsupported", message="认证检查不写入远端目录", error=None),
                    "data_port": dict(state="unsupported", message="认证检查不打开数据端口", error=None),
                }
                return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False,
                            authentication=state, checks=checks)
            try:
                payload, probe_error = self.run_auth_probe(profile)
            except RpcError as exc:
                failed = dict(state="unsupported", message=exc.message, error=detail(exc.code, exc.message))
                checks = {
                    "dns_control_port": dict(state="pending", message="未执行", error=None),
                    "tls_identity": dict(state="pending", message="未执行", error=None),
                    "login": failed,
                    "capabilities": dict(state="pending", message="未执行", error=None),
                    "directory_permission": dict(state="unsupported", message="认证检查不写入远端目录", error=None),
                    "data_port": dict(state="unsupported", message="认证检查不打开数据端口", error=None),
                }
                return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False,
                            authentication="unknown", checks=checks)
            if payload is None:
                checks = {
                    "dns_control_port": dict(state="failed", message=probe_error, error=detail("protocol_error", probe_error)),
                    "tls_identity": dict(state="pending", message="未执行", error=None),
                    "login": dict(state="pending", message="未执行", error=None),
                    "capabilities": dict(state="pending", message="未执行", error=None),
                    "directory_permission": dict(state="unsupported", message="认证检查不写入远端目录", error=None),
                    "data_port": dict(state="unsupported", message="认证检查不打开数据端口", error=None),
                }
                return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False,
                            authentication="failed", checks=checks)
            def check(key, default_message):
                state = payload.get(key, "pending")
                if state not in {"pending", "running", "passed", "failed", "unsupported", "skipped"}:
                    state = "failed"
                message = default_message if not probe_error else probe_error
                return dict(state=state, message=message, error=(
                    detail("auth_failed", probe_error) if state == "failed" and probe_error else None))
            checks = {
                "dns_control_port": check("dns_control_port", "控制端口已连接"),
                "tls_identity": check("tls_identity", "TLS 服务端身份已验证"),
                "login": check("login", "token 登录已验证"),
                "capabilities": check("capabilities", "V3 控制面能力已协商"),
                "directory_permission": dict(state="unsupported", message="认证检查不写入远端目录", error=None),
                "data_port": dict(state="unsupported", message="认证检查不打开数据端口", error=None),
            }
            authentication = payload.get("authentication", "failed")
            if authentication not in {"unknown", "not_required", "required", "authenticating", "authenticated", "expired", "failed", "keyring_locked"}:
                authentication = "failed"
            return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False,
                        authentication=authentication, checks=checks)
        if method == "node.capabilities":
            if self.args.demo:
                return dict(kind="capabilities", node_id=p["node_id"], adapter="demo", observed_at=now(), protocol_version="not-connected", modes=[], schedulers=[], range_policies=[], max_channel_count=1, max_pending_window=0, max_queue_depth=0, checksums=[], resume_supported=False, pause_supported=False, control_tls_verified=False, data_tls_supported=False, directory_permission_probe_supported=False, mode_capabilities=[])
            self.profile_for({"node_id":p["node_id"]})
            caps=dict(schedulers=["static","dynamic_ring"],range_policies=["auto"],checksums=["crc32c","none"],resume_supported=True,pause_supported=False,control_tls_verified=False,data_tls_supported=True,max_channel_count=10,max_pending_window=16,max_queue_depth=320)
            return dict(kind="capabilities",node_id=p["node_id"],adapter="real",observed_at=now(),protocol_version="v3-local-supported-not-peer-observed",modes=["v3"],directory_permission_probe_supported=False,mode_capabilities=[dict(mode="v3",**caps)],**caps)
        if method == "profile.list": return dict(kind="profile_list", profiles=list(self.store["profiles"].values()))
        if method == "transfer.list":
            page=self.page("transfer_list",dict(transfer_id="*",revision=0),p,
                list(self.store["tasks"].values()),[method,p["limit"]])
            return dict(kind="transfer_list",snapshots=page["items"],next_cursor=page["next_cursor"])
        if method == "transfer.get": return self.snapshot(self.task(p))
        if method in {"attempt.list", "file.list", "range.list"}:
            t=self.task(p); attempt_id=p.get("attempt_id")
            query=[method,p["transfer_id"],attempt_id,p.get("file_id"),p.get("generation"),p["limit"]]
            if method == "attempt.list":
                return self.page("attempt_list",t,p,self.attempts_for(t["transfer_id"]),query,{"attempt_id":None})
            if method == "file.list":
                items=self.demo_files(t,attempt_id) if self.args.demo else self.real_files(t,attempt_id)
                observed=self.args.demo or self.summary_for(t,attempt_id) is not None
                meta=dict(observation="simulated" if self.args.demo else "final_summary" if observed else "not_available",
                    coverage="observed_only")
                return self.page("file_list",t,p,items,query,meta)
            items=self.demo_ranges(t,attempt_id,p["file_id"],p["generation"]) if self.args.demo else self.real_ranges(t,p)
            return self.page("range_list",t,p,items,query,{"file_id":p["file_id"],"generation":p["generation"],
                "observation":"simulated" if self.args.demo else "final_summary", "coverage":"observed_only"})
        if method == "transfer.subscribe":
            after = p["after_sequence"]
            reset = p["agent_epoch"] != self.epoch or after is None or after > self.sequence or (self.events and after < self.events[0]["sequence"]-1)
            client.subscription = uid(); client.ids = set(p["transfer_ids"])
            snapshots = [copy.deepcopy(t) for t in self.store["tasks"].values() if not client.ids or t["transfer_id"] in client.ids] if reset else []
            client.replay = [] if reset else [e for e in self.events if e["sequence"] > after]
            return dict(kind="subscription", subscription_id=client.subscription, agent_epoch=self.epoch, sequence=self.sequence if reset else after, reset=bool(reset), snapshots=snapshots)
        opid = p.get("operation_id")
        fingerprint = json.dumps([method, p], sort_keys=True)
        if opid in self.store["operations"]:
            old = self.store["operations"][opid]
            if old["request"] != fingerprint: raise RpcError("invalid_state", "operation_id 已用于不同请求")
            return copy.deepcopy(old["result"])
        if len(self.store["operations"]) >= MAX_OPERATIONS: raise RpcError("invalid_state", "本地操作历史预算已满")
        if method == "node.network.clear":
            if p.get("confirm") is not True: raise RpcError("invalid_state","必须显式确认清除绑定")
            old=self.store["network_bindings"].get(p["node_id"])
            if (old["plan_id"] if old else None)!=p["expected_plan_id"]:
                raise RpcError("revision_conflict","绑定已变化；读取最新绑定后再确认清除")
            if any(t["state"] in ACTIVE and p["node_id"] in [t["source"]["node_id"],t["destination"]["node_id"]] for t in self.store["tasks"].values()):
                raise RpcError("invalid_state","节点有传输任务运行，不能清除绑定")
            self.store["network_bindings"].pop(p["node_id"],None)
            result=dict(kind="network_configuration",node_id=p["node_id"],binding=None)
        elif method == "node.network.apply":
            plan = self.store.get("network_plans", {}).get(p["plan_id"])
            if plan is None or plan.get("node_id") != p["node_id"]:
                raise RpcError("not_found", "网卡绑定方案不存在")
            if not p["confirm"]:
                raise RpcError("invalid_state", "必须显式确认网卡绑定方案")
            current = self._network_inventory(p["node_id"])
            if current["inventory_revision"] != p["inventory_revision"] or plan["inventory_revision"] != p["inventory_revision"]:
                raise RpcError("revision_conflict", "网卡清单已变化，请重新检测")
            if not plan["bindings"]:
                raise RpcError("invalid_state", "没有可应用的网卡地址绑定")
            if any(t["state"] in ACTIVE and p["node_id"] in [t["source"]["node_id"],t["destination"]["node_id"]] for t in self.store["tasks"].values()): raise RpcError("invalid_state","节点有运行任务，不能修改绑定")
            if len(plan["bindings"]) != plan["requested_links"]:
                raise RpcError("invalid_state", "可用网卡不足，不能应用不完整的链路方案；请降低链路数后重新生成")
            old = self.store.get("network_bindings", {}).get(p["node_id"])
            outcome = "already_applied" if old and old.get("plan_id") == p["plan_id"] else "applied"
            self.store["network_bindings"][p["node_id"]] = dict(
                plan_id=p["plan_id"], inventory_revision=p["inventory_revision"],
                bindings=copy.deepcopy(plan["bindings"]),
                local_bind_addresses=[x["local_address"] for x in plan["bindings"]],
                applied_at=now())
            result = dict(kind="network_apply", node_id=p["node_id"], plan_id=p["plan_id"],
                          inventory_revision=p["inventory_revision"], outcome=outcome,
                          effective_bindings=copy.deepcopy(plan["bindings"]),
                          requires_privilege=False)
        elif method.startswith("profile."):
            profiles = self.store["profiles"]
            if method == "profile.delete":
                if any(t["state"] in ACTIVE and p["node_id"] in [t["source"]["node_id"], t["destination"]["node_id"]] for t in self.store["tasks"].values()): raise RpcError("invalid_state", "节点有运行中的任务")
                if p["node_id"] not in profiles: raise RpcError("not_found", "节点不存在")
                del profiles[p["node_id"]]; result = dict(kind="deleted", node_id=p["node_id"])
            else:
                profile = p["profile"]
                if len(profiles) >= 100 and profile["node_id"] not in profiles: raise RpcError("invalid_state", "节点预算已满")
                if method == "profile.create" and profile["node_id"] in profiles: raise RpcError("invalid_state", "节点已存在")
                if method == "profile.update" and profile["node_id"] not in profiles: raise RpcError("not_found", "节点不存在")
                profiles[profile["node_id"]] = copy.deepcopy(profile); result = dict(kind="profile", profile=profile)
        elif method == "transfer.create":
            if len(self.store["tasks"]) >= MAX_TASKS: raise RpcError("invalid_state", "任务预算已满")
            self.validation(p)
            if self.args.demo:
                metrics = {k:None for k in self.schema["$defs"]["metrics"]["properties"]}
                adapter="demo"
            else:
                path, remote, profile = self.validate_real_request(p)
                metrics = {k:None for k in self.schema["$defs"]["metrics"]["properties"]}
                if path.is_file():
                    metrics["files_total"] = 1
                    metrics["bytes_total"] = path.stat().st_size
                adapter="real"
            transfer_id=uid()
            remote=p["destination"] if p["direction"]=="upload" else p["source"]
            evidence=empty_evidence()
            evidence["event_log"]=str(self.state_dir / (transfer_id+".events.jsonl"))
            evidence["summary"]=str(self.state_dir / (transfer_id+".summary.json"))
            t = dict(transfer_id=transfer_id, revision=0, adapter=adapter, state="queued", direction=p["direction"], source=p["source"], destination=p["destination"], policy=p["policy"], actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None, attempt=dict(attempt_id=uid(), attempt_number=1, state="queued", cancel_requested=False, started_at=None, ended_at=None, quiescence="not_applicable"), effective_policy=self.pending_policy(), metrics=metrics, recovery=dict(eligibility="unknown", checkpoint_id=None, reason=None), scan_complete=False, commit_confirmed=False, transfer_status="pending", integrity_status="pending", evidence_status="pending", error=None, backend_evidence=evidence)
            self.store["tasks"][t["transfer_id"]] = t
            if adapter=="real": self.store["task_inputs"][transfer_id]=dict(profile=copy.deepcopy(profile),binding=copy.deepcopy(self.store["network_bindings"].get(remote["node_id"])))
            self.store["attempt_history"][t["transfer_id"]] = [copy.deepcopy(t["attempt"])]
            self.emit(t); result = self.snapshot(t)
        else:
            if self.args.demo: self.ensure_demo()
            else: self.ensure_real()
            t = self.task(p)
            if p["expected_revision"] != t["revision"]: raise RpcError("revision_conflict", "任务已更新，请刷新后重试")
            outcome = "accepted"
            if method == "transfer.cancel":
                if t["state"] in TERMINAL: outcome = "already_terminal"
                elif t["state"] == "queued":
                    t.update(state="cancelled"); t["attempt"].update(state="cancelled", ended_at=now(), quiescence="confirmed"); self.emit(t)
                else:
                    t["attempt"].update(state="cancelling", cancel_requested=True, quiescence="pending"); self.emit(t)
                    proc=self.processes.get(t["transfer_id"])
                    if proc is not None:
                        proc.terminate()
            elif method == "transfer.start":
                if t["state"] in ACTIVE: outcome = "already_running"
                elif t["state"] != "queued": raise RpcError("invalid_state", "请按恢复资格使用恢复")
                else: self.begin(t)
            elif method == "transfer.resume":
                if t["state"] not in TERMINAL or t["recovery"]["eligibility"] != "eligible" or p["checkpoint_id"] != t["recovery"]["checkpoint_id"]:
                    raise RpcError("checkpoint_missing", "agent 未返回可验证的 resume checkpoint")
                if t["policy"]["resume_policy"]=="never": raise RpcError("unsupported","任务策略禁止恢复")
                t["attempt"] = dict(attempt_id=uid(), attempt_number=t["attempt"]["attempt_number"]+1, state="queued", cancel_requested=False, started_at=None, ended_at=None, quiescence="not_applicable")
                t.update(error=None, transfer_status="pending", integrity_status="pending", evidence_status="pending")
                t["effective_policy"]=self.pending_policy(); t["commit_confirmed"]=False; t["scan_complete"]=False
                prefix=t["transfer_id"]+"."+t["attempt"]["attempt_id"]
                t["backend_evidence"]=empty_evidence()
                t["backend_evidence"].update(event_log=str(self.state_dir/(prefix+".events.jsonl")),summary=str(self.state_dir/(prefix+".summary.json")))
                self.begin(t)
            result = dict(kind="operation", outcome=outcome, snapshot=copy.deepcopy(t))
        self.store["operations"][opid] = dict(request=fingerprint, result=copy.deepcopy(result))
        self.save()
        return result
    def begin(self, t):
        if not self.args.demo:
            return self.begin_real(t)
        t.update(state="scanning", actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None)
        t["attempt"].update(state="running", started_at=now(), ended_at=None)
        self.emit(t)
        self.runners[t["transfer_id"]] = asyncio.create_task(self.demo(t))
    def begin_real(self, t):
        t.update(state="scanning", actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None)
        t["attempt"].update(state="running", started_at=now(), ended_at=None)
        self.emit(t)
        self.runners[t["transfer_id"]] = asyncio.create_task(self.run_real(t))

    async def run_real(self, t):
        proc = None
        try:
            self.validate_real_request(t)
            command = self.build_real_command(t)
            t["state"] = "connecting"; self.emit(t)
            proc = await asyncio.create_subprocess_exec(*command, stdout=asyncio.subprocess.PIPE,
                                                        stderr=asyncio.subprocess.PIPE)
            self.processes[t["transfer_id"]] = proc
            t["state"] = "transferring"
            # Do not publish requested/default policy as actual backend state.
            # The summary is read after the process exits and is the only source
            # used to resolve mode, scheduler, checksum, and resume.
            self.emit(t)
            stdout, stderr = await proc.communicate()
            summary_path=t["backend_evidence"].get("summary")
            summary=self.load_summary(summary_path) or {}
            self.read_real_evidence(t,summary)
            if summary: self.store.setdefault("summaries",{})[t["attempt"]["attempt_id"]]=summary_path
            self.apply_real_policy(t, summary)
            if t["attempt"]["cancel_requested"]:
                t.update(state="cancelled", transfer_status="fail", evidence_status="incomplete")
                t["attempt"].update(state="cancelled", ended_at=now(), quiescence="confirmed")
                self.checkpoint(t)
            elif proc.returncode == 0 and summary.get("result") == "pass" and self.completion_matches(t,summary):
                t.update(state="completed", commit_confirmed=True, transfer_status="pass",
                         integrity_status="not_requested" if t["policy"]["checksum_policy"]=="none" else "pass", evidence_status="complete",
                         recovery=dict(eligibility="ineligible", checkpoint_id=None, reason="backend完成"))
                t["backend_evidence"]["finalize"]={"status":"pass"}
                t["attempt"].update(state="completed", ended_at=now(), quiescence="confirmed")
                self.quiescent(t)
            else:
                mismatch=summary.get("result")=="pass"
                message=stderr.decode(errors="replace")[-2000:] or (
                    "V3 核心成功，但实际策略或文件/字节计数与预检不一致；请查看 transfer.evidence"
                    if mismatch else "CPNetFlux core 返回失败")
                t.update(state="recoverable", transfer_status="fail", integrity_status="fail",
                         evidence_status="incomplete",
                         error=detail("capability_mismatch" if mismatch else "internal_error",message,t["transfer_id"],"transfer"))
                t["attempt"].update(state="recoverable", ended_at=now(), quiescence="confirmed")
                self.checkpoint(t)
                if t["recovery"]["eligibility"]!="eligible": t["state"]="failed"; t["attempt"]["state"]="failed"
            self.emit(t)
        except (OSError, RpcError) as exc:
            t.update(state="failed", transfer_status="fail", evidence_status="incomplete",
                     error=detail(getattr(exc,"code","internal_error"),str(exc),t["transfer_id"],"transfer"))
            t["attempt"].update(state="failed", ended_at=now(), quiescence="confirmed")
            self.emit(t)
        finally:
            self.processes.pop(t["transfer_id"], None)
            self.runners.pop(t["transfer_id"], None)

    async def demo(self, t):
        try:
            for phase in ["connecting", "negotiating", "transferring"]:
                await asyncio.sleep(self.args.tick_ms/1000)
                if self.finish_cancel(t): return
                t["state"] = phase
                if phase == "connecting":
                    t["scan_complete"] = True
                    t["metrics"].update(files_total=4, files_committed=0, files_failed=0, files_skipped=0, bytes_total=4194304, bytes_transferred=0, bytes_verified=0, bytes_committed=0)
                if phase == "transferring":
                    options=self.resolve_options(t["policy"])
                    scheduler="dynamic_ring" if options["file_scheduling"]=="dynamic" else "static"
                    t.update(actual_mode="v3",actual_scheduler=scheduler,actual_range_policy="auto")
                    t["effective_policy"]=dict(resolution="resolved",mode="v3",scheduler=scheduler,range_policy="auto",checksum=options["checksum"],resume_enabled=t["policy"]["resume_policy"]!="never",control_tls="verified",data_tls="verified" if t["policy"]["data_tls"]=="required" else "off",channel_count=options["channel_count"],applied_at=now())
                    t["metrics"].update(channel_count_active=1, data_connections=1, queue_depth_current=4)
                self.emit(t)
            for index in range(1, 9):
                await asyncio.sleep(self.args.tick_ms/1000)
                if self.finish_cancel(t): return
                m = t["metrics"]; m.update(bytes_transferred=index*524288, bytes_verified=index*524288, bytes_committed=(index//2)*1048576, files_committed=index//2, queue_depth_current=4-index//2)
                if index == 4 and "demo-fail" in t["destination"]["path"] and t["attempt"]["attempt_number"] == 1:
                    t.update(state="failed", transfer_status="fail", error=detail("internal_error", "演示故障：未传输任何真实文件，可恢复演示任务", t["transfer_id"], "transfer"))
                    t["attempt"].update(state="failed", ended_at=now()); self.checkpoint(t); self.emit(t); return
                self.emit(t)
            await asyncio.sleep(self.args.tick_ms/1000)
            if self.finish_cancel(t): return
            t["state"] = "committing"; self.emit(t)
            await asyncio.sleep(self.args.tick_ms/1000)
            if self.finish_cancel(t): return
            t.update(state="completed", commit_confirmed=True, transfer_status="pass", integrity_status="pass" if t["policy"]["checksum_policy"] != "none" else "not_requested", evidence_status="complete", recovery=dict(eligibility="ineligible", checkpoint_id=None, reason="演示已完成"))
            t["attempt"].update(state="completed", ended_at=now()); self.quiescent(t); self.emit(t)
        finally: self.runners.pop(t["transfer_id"], None)
    def checkpoint(self, t):
        if t["policy"]["resume_policy"]=="never":
            t["recovery"]=dict(eligibility="ineligible",checkpoint_id=None,reason="任务策略禁止恢复")
            self.quiescent(t);return
        if self.args.demo:
            t["recovery"] = dict(eligibility="eligible", checkpoint_id="demo:"+t["transfer_id"], reason="仅用于演示恢复")
        else:
            summary = t.get("backend_evidence", {}).get("summary")
            evidence = t.get("backend_evidence", {}).get("manifest") or {}
            if summary and evidence.get("status") == "observed":
                t["recovery"] = dict(eligibility="eligible", checkpoint_id="core:"+t["transfer_id"], reason="核心 manifest 可用于 resume")
            else:
                t["recovery"] = dict(eligibility="unknown", checkpoint_id=None, reason="核心未返回可验证 manifest；resume 已禁用")
        self.quiescent(t)
    def quiescent(self, t): t["metrics"].update(speed_bps=None, average_speed_bps=None, eta_seconds=None, channel_count_active=0, data_connections=0)
    def finish_cancel(self, t):
        if not t["attempt"]["cancel_requested"]: return False
        t.update(state="cancelled", transfer_status="fail")
        t["attempt"].update(state="cancelled", ended_at=now(), quiescence="confirmed"); self.checkpoint(t); self.emit(t); return True

class Client:
    def __init__(self, agent, reader, writer):
        self.agent, self.reader, self.writer = agent, reader, writer
        self.queue = asyncio.Queue(maxsize=256)
        self.subscription = None; self.ids = set(); self.replay = []
    def send(self, message):
        try: self.queue.put_nowait(message)
        except asyncio.QueueFull: self.writer.close()
    def event(self, event):
        if self.subscription and (not self.ids or event["transfer_id"] in self.ids):
            self.send(dict(jsonrpc="2.0", method="transfer.event", params=dict(event, subscription_id=self.subscription)))
    async def output(self):
        while True:
            msg = await self.queue.get()
            self.writer.write((json.dumps(msg, ensure_ascii=False, allow_nan=False)+"\n").encode())
            await asyncio.wait_for(self.writer.drain(), 3)
            self.queue.task_done()
    def error(self, rid, code, message, number):
        self.send(dict(jsonrpc="2.0", id=rid, error=dict(code=number, message=message, data=detail(code, message))))
    async def run(self):
        output = asyncio.create_task(self.output())
        try:
            while not self.writer.is_closing():
                try: line = await self.reader.readline()
                except (ValueError, asyncio.LimitOverrunError):
                    self.error(None, "protocol_error", "消息超过1MiB限制", -32600)
                    await asyncio.wait_for(self.queue.join(), 3); break
                if not line: break
                if len(line) > MAX_FRAME:
                    self.error(None, "protocol_error", "消息超过1MiB限制", -32600)
                    await asyncio.wait_for(self.queue.join(), 3); break
                rid = None
                try:
                    req = json.loads(line, parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
                    if isinstance(req, dict) and isinstance(req.get("id"), str): rid = req["id"]
                    if not isinstance(req, dict) or req.get("jsonrpc") != "2.0" or rid is None: raise RpcError("protocol_error", "无效JSON-RPC请求", -32600)
                    method = req.get("method")
                    known = [x["properties"]["method"]["const"] for x in self.agent.schema["$defs"]["request"]["oneOf"]]
                    if method not in known: raise RpcError("protocol_error", "未知方法", -32601)
                    self.agent.validator.validate(req)
                    result = self.agent.dispatch(method, req["params"], self)
                    self.send(dict(jsonrpc="2.0", id=rid, result=result))
                    if method == "transfer.subscribe":
                        for event in self.replay: self.event(event)
                        self.replay = []
                except (json.JSONDecodeError, UnicodeDecodeError, ValueError, RecursionError): self.error(rid, "protocol_error", "JSON解析失败", -32700)
                except ValidationError: self.error(rid, "protocol_error", "参数不符合桌面契约", -32602)
                except RpcError as exc: self.error(rid, exc.code, exc.message, exc.number)
        except (ConnectionError, asyncio.TimeoutError): pass
        finally:
            self.agent.clients.discard(self); output.cancel()
            await asyncio.gather(output, return_exceptions=True)
            self.writer.close()
            try: await self.writer.wait_closed()
            except ConnectionError: pass

async def serve(args, schema):
    agent = Agent(args, schema)
    path = agent.runtime / "agent.sock"
    if path.exists() or path.is_symlink():
        info = path.lstat()
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid(): raise RuntimeError("unsafe existing socket")
        path.unlink()  # Exclusive lock is held; only our stale socket may be removed.
    async def connect(reader, writer):
        peer = writer.get_extra_info("socket")
        _, peer_uid, _ = struct.unpack("3i", peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if peer_uid != os.getuid() or len(agent.clients) >= MAX_CLIENTS:
            writer.close(); return
        client = Client(agent, reader, writer); agent.clients.add(client)
        await client.run()
    server = await asyncio.start_unix_server(connect, str(path), limit=MAX_FRAME)
    os.chmod(path, 0o600)
    stop = asyncio.Event(); loop = asyncio.get_running_loop()
    for sig in [signal.SIGTERM, signal.SIGINT]: loop.add_signal_handler(sig, stop.set)
    try:
        async with server: await stop.wait()
    finally:
        for runner in list(agent.runners.values()): runner.cancel()
        await asyncio.gather(*list(agent.runners.values()), return_exceptions=True)
        agent.save()
        for client in list(agent.clients): client.writer.close()
        path.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description="CPNetFlux桌面agent；--demo仅模拟，不传输文件")
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--runtime-dir", default=str(Path(os.environ.get("XDG_RUNTIME_DIR", "/nonexistent"))/"cpnetflux"))
    parser.add_argument("--state-dir", default=str(Path(os.environ.get("XDG_STATE_HOME", str(Path.home()/".local/state")))/"cpnetflux-desktop"))
    parser.add_argument("--tick-ms", type=int, default=400)
    parser.add_argument("--core-bin-dir", default=os.environ.get("CPNETFLUX_CORE_BIN_DIR"))
    args = parser.parse_args()
    if args.runtime_dir == "/nonexistent/cpnetflux": parser.error("XDG_RUNTIME_DIR缺失，需指定私有 --runtime-dir")
    if not 10 <= args.tick_ms <= 10000: parser.error("tick-ms范围10..10000")
    schema = json.loads((Path(__file__).resolve().parents[1]/"schemas/desktop-agent.schema.json").read_text())
    os.umask(0o077)
    try: asyncio.run(serve(args, schema))
    except (RuntimeError, OSError): parser.exit(1, "agent启动/状态存储失败：检查私有目录、权限及单实例锁；未启动真实传输\n")
if __name__ == "__main__": main()
