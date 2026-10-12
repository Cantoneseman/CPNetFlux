#!/usr/bin/env python3
"""CPNetFlux local shell agent. Demo never opens or transfers user files."""
import argparse
import asyncio
from collections import deque
import copy
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import signal
import socket
import stat
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
API_VERSION = "2.0.0"
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
        self.store = dict(tasks={}, profiles={}, operations={}, attempt_history={})
        self.page_snapshots, self.page_cursors = {}, {}
        path = self.state_dir / "state.json"
        if path.exists():
            if path.is_symlink() or path.stat().st_uid != os.getuid(): raise RuntimeError("unsafe state file")
            if path.stat().st_size > 8 * MAX_FRAME: raise RuntimeError("state file exceeds limit")
            self.store = json.loads(path.read_text())
            self.store.setdefault("attempt_history", {})
            for t in self.store["tasks"].values():
                t.setdefault("backend_evidence", empty_evidence())
                t.setdefault("effective_policy", self.pending_policy())
                t["attempt"].setdefault("quiescence", "not_applicable")
                if t["state"] in ACTIVE:
                    t.update(state="recoverable", revision=t["revision"]+1)
                    t["attempt"]["state"] = "recoverable"
                    t["attempt"]["quiescence"] = "confirmed"
                    t["metrics"].update(speed_bps=None, average_speed_bps=None, eta_seconds=None, channel_count_active=0, data_connections=0)
                    t["recovery"] = dict(eligibility="eligible", checkpoint_id="demo:"+t["transfer_id"], reason="演示 agent 重启，需手动恢复")
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

    def profile_for(self, endpoint):
        node_id = endpoint.get("node_id")
        profile = self.store["profiles"].get(node_id)
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
        profile = self.profile_for(remote)
        # The unified tree adapter is the only authenticated real endpoint currently
        # exposed by the core. Keep single-file requests explicit and actionable until
        # the file client gains the same control-plane contract.
        if path.is_file():
            raise RpcError("unsupported", "核心 file client 尚未提供统一 TLS/认证控制面；请使用目录任务，或先升级 core adapter")
        return path, remote, profile

    def build_real_command(self, t):
        path = Path(t["source"]["path"] if t["direction"] == "upload" else t["destination"]["path"])
        remote = t["destination"] if t["direction"] == "upload" else t["source"]
        profile = self.profile_for(remote)
        args = ["--host", profile["host"], "--port", str(profile["control_port"]),
                "--connections", "1", "--checksum", "crc32c",
                "--tls-mode", "required", "--tls-ca-file", profile["ca_path"],
                "--data-tls-mode", "required", "--event-log", t["backend_evidence"]["event_log"],
                "--auth-mode", "token", "--auth-token-file", profile["credential_ref"][5:],
                "--json-summary", t["backend_evidence"]["summary"], "--phase-timing", "on"]
        if t["attempt"]["attempt_number"] > 1:
            args += ["--resume"]
        if t["direction"] == "upload":
            exe = self.core_binary("cpnetflux-tree-upload-client")
            args += ["--source-dir", str(path), "--dest-dir", remote["path"]]
        else:
            exe = self.core_binary("cpnetflux-tree-download-client")
            args += ["--source-dir", remote["path"], "--dest-dir", str(path)]
        return [exe] + args

    def apply_real_policy(self, t, summary):
        """Copy only backend-observed facts into the public transfer snapshot.

        The tree client summary is authoritative for mode/scheduler/checksum and
        resume. TLS remains unknown because the current client summary does not
        expose a verified handshake fact.
        """
        observed_mode = {"persistent_tree": "v2", "per_file": "v1"}.get(
            summary.get("actual_mode"), "unknown")
        observed_scheduler = {"dynamic": "dynamic_ring", "static": "static",
                              "per_file": "static"}.get(
            summary.get("actual_file_scheduling"), "unknown")
        observed_checksum = {"crc32c": "crc32c", "none": "none"}.get(
            summary.get("checksum_algorithm"), "unknown")
        successful = summary.get("result") == "pass"
        t["actual_mode"] = observed_mode
        t["actual_scheduler"] = observed_scheduler
        t["actual_range_policy"] = "file" if observed_mode != "unknown" else "unknown"
        connections = summary.get("connections")
        if not isinstance(connections, int) or connections < 1:
            connections = None
        t["effective_policy"] = dict(
            resolution="resolved" if successful and observed_mode != "unknown" else "pending",
            mode=observed_mode, scheduler=observed_scheduler,
            range_policy=t["actual_range_policy"], checksum=observed_checksum,
            resume_enabled=summary.get("resume") if isinstance(summary.get("resume"), bool) else None,
            control_tls="unknown", data_tls="unknown", channel_count=connections,
            applied_at=now() if successful and observed_mode != "unknown" else None)

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
                     bytes_committed=moved, channel_count_active=0, queue_depth_current=0,
                     data_connections=summary.get("data_connect_count"), speed_bps=None,
                     average_speed_bps=None, eta_seconds=None)
            t["backend_evidence"]["phase_timing"].update(
                control_prepare=summary.get("control_prepare_seconds"),
                payload_io=summary.get("elapsed_seconds"),
                transfer_complete_wait=summary.get("transfer_complete_wait_seconds"))
            t["backend_evidence"]["checksum"] = {"algorithm": summary.get("checksum_algorithm", "unknown"),
                                                        "status": "pass" if summary.get("result") == "pass" else "fail"}
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
                            items=copy.deepcopy(items), expires=now_mono+PAGE_TTL_SECONDS)
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
        if extra: result.update(extra)
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
    def dispatch(self, method, p, client):
        if method == "agent.getInfo":
            real_ready = (self.core_bin_dir is not None and self.core_bin_dir.is_dir() and
                          all((self.core_bin_dir / name).is_file() and os.access(self.core_bin_dir / name, os.X_OK)
                              for name in ("cpnetflux-tree-upload-client", "cpnetflux-tree-download-client")))
            return dict(kind="agent_info",api_version=API_VERSION,agent_version="0.1.0",agent_epoch=self.epoch,
                        adapter="demo" if self.args.demo else "real",max_frame_bytes=MAX_FRAME,
                        max_concurrent_requests=32,state_store="persistent",
                        features=["profiles","transfer_lifecycle","entity_pages","resume"] if self.args.demo else
                                 (["profiles","transfer_lifecycle","entity_pages","cancel","resume"] if real_ready else ["profiles"]))
        if method == "transfer.pause": raise RpcError("unsupported", "暂停尚未实现，任务状态保持不变")
        if method == "directory.list": raise RpcError("unsupported", "真实远端目录浏览尚未接入")
        if method == "node.testConnection":
            if self.args.demo:
                checks = {k:dict(state="unsupported", message="开发诊断模式不连接远端", error=None)
                          for k in ["dns_control_port", "tls_identity", "login", "capabilities", "directory_permission", "data_port"]}
                return dict(kind="connection_test", node_id=p["node_id"], adapter="demo", ready=False, authentication="unknown", checks=checks)
            try:
                profile=self.profile_for({"node_id":p["node_id"]})
                ca=profile.get("ca_path")
                tls_ok=bool(ca and Path(ca).is_file() and os.access(ca, os.R_OK))
                with socket.create_connection((profile["host"], int(profile["control_port"])), timeout=2):
                    port_ok=True
                checks={
                    "dns_control_port":dict(state="pass" if port_ok else "fail", message="控制端口可达" if port_ok else "控制端口不可达", error=None),
                    "tls_identity":dict(state="pass" if tls_ok else "fail", message="CA 可读" if tls_ok else "CA 不存在或不可读", error=None),
                    "login":dict(state="unsupported", message="真实认证需由核心 tree client 完成", error=None),
                    "capabilities":dict(state="unsupported", message="真实能力需通过受控控制面协商", error=None),
                    "directory_permission":dict(state="unsupported", message="尚未执行远端目录权限探测", error=None),
                    "data_port":dict(state="unsupported", message="数据端口由核心协商", error=None)}
                return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=port_ok and tls_ok, authentication="pending", checks=checks)
            except RpcError: raise
            except (OSError, ValueError) as exc:
                return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False, authentication="unknown",
                            checks={"dns_control_port":dict(state="fail",message=str(exc),error=detail("data_port_unreachable",str(exc))),
                                    "tls_identity":dict(state="unknown",message="未执行",error=None),
                                    "login":dict(state="unknown",message="未执行",error=None),
                                    "capabilities":dict(state="unknown",message="未执行",error=None),
                                    "directory_permission":dict(state="unknown",message="未执行",error=None),
                                    "data_port":dict(state="unknown",message="未执行",error=None)})
        if method == "node.capabilities":
            if self.args.demo:
                return dict(kind="capabilities", node_id=p["node_id"], adapter="demo", observed_at=now(), protocol_version="not-connected", modes=[], schedulers=[], range_policies=[], max_channel_count=1, max_pending_window=0, max_queue_depth=0, checksums=[], resume_supported=False, pause_supported=False, control_tls_verified=False, data_tls_supported=False, directory_permission_probe_supported=False, mode_capabilities=[])
            return dict(kind="capabilities", node_id=p["node_id"], adapter="real", observed_at=now(), protocol_version="core-tree-client", modes=["v2"], schedulers=["dynamic_ring"], range_policies=["file"], max_channel_count=1, max_pending_window=16, max_queue_depth=256, checksums=["crc32c"], resume_supported=True, pause_supported=False, control_tls_verified=False, data_tls_supported=True, directory_permission_probe_supported=False, mode_capabilities=[dict(mode="v2", schedulers=["dynamic_ring"], range_policies=["file"], checksums=["crc32c"], resume_supported=True, pause_supported=False, control_tls_verified=False, data_tls_supported=True, max_channel_count=1, max_pending_window=16, max_queue_depth=256)])
        if method == "profile.list": return dict(kind="profile_list", profiles=list(self.store["profiles"].values()))
        if method == "transfer.list":
            if p["cursor"] is not None: raise RpcError("cursor_expired", "本切片仅支持完整第一页，请刷新")
            return dict(kind="transfer_list", snapshots=list(self.store["tasks"].values())[:p["limit"]], next_cursor=None)
        if method == "transfer.get": return self.snapshot(self.task(p))
        if method in {"attempt.list", "file.list", "range.list"}:
            t=self.task(p); attempt_id=p.get("attempt_id")
            query=[method,p["transfer_id"],attempt_id,p.get("file_id"),p.get("generation"),p["limit"]]
            if method == "attempt.list":
                return self.page("attempt_list",t,p,self.attempts_for(t["transfer_id"]),query,{"attempt_id":None})
            if not self.args.demo:
                raise RpcError("unsupported", "真实 manifest 文件实体分页尚未由 agent adapter 提供；任务级摘要和阶段证据可用")
            if method == "file.list":
                return self.page("file_list",t,p,self.demo_files(t,attempt_id),query)
            return self.page("range_list",t,p,self.demo_ranges(t,attempt_id,p["file_id"],p["generation"]),query,{"file_id":p["file_id"],"generation":p["generation"]})
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
        if method.startswith("profile."):
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
            evidence=empty_evidence()
            evidence["event_log"]=str(self.state_dir / (transfer_id+".events.jsonl"))
            evidence["summary"]=str(self.state_dir / (transfer_id+".summary.json"))
            t = dict(transfer_id=transfer_id, revision=0, adapter=adapter, state="queued", direction=p["direction"], source=p["source"], destination=p["destination"], policy=p["policy"], actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None, attempt=dict(attempt_id=uid(), attempt_number=1, state="queued", cancel_requested=False, started_at=None, ended_at=None, quiescence="not_applicable"), effective_policy=self.pending_policy(), metrics=metrics, recovery=dict(eligibility="unknown", checkpoint_id=None, reason=None), scan_complete=False, commit_confirmed=False, transfer_status="pending", integrity_status="pending", evidence_status="pending", error=None, backend_evidence=evidence)
            self.store["tasks"][t["transfer_id"]] = t
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
                t["attempt"] = dict(attempt_id=uid(), attempt_number=t["attempt"]["attempt_number"]+1, state="queued", cancel_requested=False, started_at=None, ended_at=None, quiescence="not_applicable")
                t.update(error=None, transfer_status="pending", integrity_status="pending", evidence_status="pending")
                self.begin(t)
            result = dict(kind="operation", outcome=outcome, snapshot=copy.deepcopy(t))
        self.store["operations"][opid] = dict(request=fingerprint, result=copy.deepcopy(result))
        self.save()
        return result
    def begin(self, t):
        if not self.args.demo:
            return self.begin_real(t)
        if t["policy"]["requested_mode"] in {"auto","v2"} and t["policy"]["fallback_policy"] == "reject": raise RpcError("capability_mismatch", "演示 adapter 仅模拟 V1；请求禁止回退")
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
            summary={}
            if summary_path and os.path.isfile(summary_path):
                try: summary=json.loads(Path(summary_path).read_text())
                except (OSError,ValueError): summary={}
            self.read_real_evidence(t,summary)
            self.apply_real_policy(t, summary)
            if t["attempt"]["cancel_requested"]:
                t.update(state="cancelled", transfer_status="fail", evidence_status="incomplete")
                t["attempt"].update(state="cancelled", ended_at=now(), quiescence="confirmed")
                self.quiescent(t)
            elif proc.returncode == 0 and summary.get("result") == "pass":
                t.update(state="completed", commit_confirmed=True, transfer_status="pass",
                         integrity_status="pass", evidence_status="complete",
                         recovery=dict(eligibility="ineligible", checkpoint_id=None, reason="backend完成"))
                t["backend_evidence"]["finalize"]={"status":"pass"}
                t["attempt"].update(state="completed", ended_at=now(), quiescence="confirmed")
                self.quiescent(t)
            else:
                message=stderr.decode(errors="replace")[-2000:] or "CPNetFlux core 返回失败"
                t.update(state="recoverable", transfer_status="fail", integrity_status="fail",
                         evidence_status="incomplete",
                         error=detail("internal_error",message,t["transfer_id"],"transfer"))
                t["attempt"].update(state="recoverable", ended_at=now(), quiescence="confirmed")
                self.checkpoint(t)
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
                    t.update(actual_mode="v1", actual_scheduler="static", actual_range_policy="file")
                    if t["policy"]["requested_mode"] in ["auto", "v2"]: t["fallback_reason"] = dict(code="mode_unsupported", message="演示 adapter 仅模拟 V1；不代表真实引擎协商", required=[t["policy"]["requested_mode"]], available=["v1-demo"])
                    t["effective_policy"] = dict(resolution="resolved",mode="v1",scheduler="static",range_policy="file",checksum="crc32c" if t["policy"]["checksum_policy"]=="required_crc32c" else "none",resume_enabled=t["policy"]["resume_policy"]!="never",control_tls="verified",data_tls="verified" if t["policy"]["data_tls"]=="required" else "off",channel_count=1,applied_at=now())
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
        if self.args.demo:
            t["recovery"] = dict(eligibility="eligible", checkpoint_id="demo:"+t["transfer_id"], reason="仅用于演示恢复")
        else:
            summary = t.get("backend_evidence", {}).get("summary")
            evidence = t.get("backend_evidence", {}).get("manifest", {})
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
