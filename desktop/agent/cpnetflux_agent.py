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
        self.store = dict(tasks={}, profiles={}, operations={})
        path = self.state_dir / "state.json"
        if path.exists():
            if path.is_symlink() or path.stat().st_uid != os.getuid(): raise RuntimeError("unsafe state file")
            if path.stat().st_size > 8 * MAX_FRAME: raise RuntimeError("state file exceeds limit")
            self.store = json.loads(path.read_text())
            for t in self.store["tasks"].values():
                if t["state"] in ACTIVE:
                    t.update(state="recoverable", revision=t["revision"]+1)
                    t["attempt"]["state"] = "recoverable"
                    t["metrics"].update(speed_bps=None, average_speed_bps=None, eta_seconds=None, channel_count_active=0, data_connections=0)
                    t["recovery"] = dict(eligibility="eligible", checkpoint_id="demo:"+t["transfer_id"], reason="演示 agent 重启，需手动恢复")
        self.save()
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
        if not self.args.demo: raise RpcError("unsupported", "真实传输 adapter 尚未接入；请显式使用 --demo 演示")
    def dispatch(self, method, p, client):
        if method == "transfer.pause": raise RpcError("unsupported", "暂停尚未实现，任务状态保持不变")
        if method == "directory.list": raise RpcError("unsupported", "真实远端目录浏览尚未接入")
        if method == "node.testConnection":
            checks = {k:dict(state="unsupported", message="真实服务器探测未接入", error=None)
                      for k in ["dns_control_port", "tls_identity", "login", "capabilities", "directory_permission", "data_port"]}
            return dict(kind="connection_test", node_id=p["node_id"], adapter="real", ready=False, authentication="unknown", checks=checks)
        if method == "node.capabilities":
            demo = p["node_id"] == "demo" and self.args.demo
            return dict(kind="capabilities", node_id=p["node_id"], adapter="demo" if demo else "real", observed_at=now(), protocol_version="shell-demo-1" if demo else "not-connected", modes=["v1"] if demo else [], schedulers=["static"] if demo else [], range_policies=["file"] if demo else [], max_channel_count=1, max_pending_window=1, max_queue_depth=1, checksums=[], resume_supported=demo, pause_supported=False, control_tls_verified=False, data_tls_supported=False, directory_permission_probe_supported=False)
        if method == "profile.list": return dict(kind="profile_list", profiles=list(self.store["profiles"].values()))
        if method == "transfer.list":
            if p["cursor"] is not None: raise RpcError("cursor_expired", "本切片仅支持完整第一页，请刷新")
            return dict(kind="transfer_list", snapshots=list(self.store["tasks"].values())[:p["limit"]], next_cursor=None)
        if method == "transfer.get": return self.snapshot(self.task(p))
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
            self.ensure_demo()
            if len(self.store["tasks"]) >= MAX_TASKS: raise RpcError("invalid_state", "演示任务预算已满")
            metrics = {k:None for k in self.schema["$defs"]["metrics"]["properties"]}
            t = dict(transfer_id=uid(), revision=0, adapter="demo", state="queued", direction=p["direction"], source=p["source"], destination=p["destination"], policy=p["policy"], actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None, attempt=dict(attempt_id=uid(), attempt_number=1, state="queued", cancel_requested=False, started_at=None, ended_at=None), metrics=metrics, recovery=dict(eligibility="unknown", checkpoint_id=None, reason=None), scan_complete=False, commit_confirmed=False, transfer_status="pending", integrity_status="pending", evidence_status="pending", error=None)
            self.store["tasks"][t["transfer_id"]] = t
            self.emit(t); result = self.snapshot(t)
        else:
            self.ensure_demo(); t = self.task(p)
            if p["expected_revision"] != t["revision"]: raise RpcError("revision_conflict", "任务已更新，请刷新后重试")
            outcome = "accepted"
            if method == "transfer.cancel":
                if t["state"] in TERMINAL: outcome = "already_terminal"
                elif t["state"] == "queued":
                    t.update(state="cancelled"); t["attempt"].update(state="cancelled", ended_at=now()); self.emit(t)
                else:
                    t["attempt"].update(state="cancelling", cancel_requested=True); self.emit(t)
            elif method == "transfer.start":
                if t["state"] in ACTIVE: outcome = "already_running"
                elif t["state"] != "queued": raise RpcError("invalid_state", "请按恢复资格使用恢复")
                else: self.begin(t)
            elif method == "transfer.resume":
                if t["state"] not in TERMINAL or t["recovery"]["eligibility"] != "eligible" or p["checkpoint_id"] != t["recovery"]["checkpoint_id"]: raise RpcError("checkpoint_missing", "没有符合资格的演示checkpoint")
                t["attempt"] = dict(attempt_id=uid(), attempt_number=t["attempt"]["attempt_number"]+1, state="queued", cancel_requested=False, started_at=None, ended_at=None)
                t.update(error=None, transfer_status="pending", integrity_status="pending", evidence_status="pending")
                self.begin(t)
            result = dict(kind="operation", outcome=outcome, snapshot=copy.deepcopy(t))
        self.store["operations"][opid] = dict(request=fingerprint, result=copy.deepcopy(result))
        self.save()
        return result
    def begin(self, t):
        if t["policy"]["requested_mode"] == "v2" and t["policy"]["fallback_policy"] == "reject": raise RpcError("capability_mismatch", "演示仅模拟V1；V2能力未实现且禁止回退")
        t.update(state="scanning", actual_mode="unknown", actual_scheduler="unknown", actual_range_policy="unknown", fallback_reason=None)
        t["attempt"].update(state="running", started_at=now(), ended_at=None)
        self.emit(t)
        self.runners[t["transfer_id"]] = asyncio.create_task(self.demo(t))
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
                    if t["policy"]["requested_mode"] in ["auto", "v2"]: t["fallback_reason"] = dict(code="mode_unsupported", message="演示adapter仅模拟V1；不代表真实引擎协商", required=["v2"], available=["v1-demo"])
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
        t["recovery"] = dict(eligibility="eligible", checkpoint_id="demo:"+t["transfer_id"], reason="仅用于演示恢复")
        self.quiescent(t)
    def quiescent(self, t): t["metrics"].update(speed_bps=None, average_speed_bps=None, eta_seconds=None, channel_count_active=0, data_connections=0)
    def finish_cancel(self, t):
        if not t["attempt"]["cancel_requested"]: return False
        t.update(state="cancelled", transfer_status="fail")
        t["attempt"].update(state="cancelled", ended_at=now()); self.checkpoint(t); self.emit(t); return True

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
    args = parser.parse_args()
    if args.runtime_dir == "/nonexistent/cpnetflux": parser.error("XDG_RUNTIME_DIR缺失，需指定私有 --runtime-dir")
    if not 10 <= args.tick_ms <= 10000: parser.error("tick-ms范围10..10000")
    schema = json.loads((Path(__file__).resolve().parents[1]/"schemas/desktop-agent.schema.json").read_text())
    os.umask(0o077)
    try: asyncio.run(serve(args, schema))
    except (RuntimeError, OSError): parser.exit(1, "agent启动/状态存储失败：检查私有目录、权限及单实例锁；未启动真实传输\n")
if __name__ == "__main__": main()
