"""Design-only QObject facade. No socket, process, secret or transfer access."""
import copy
import json
from pathlib import Path

from PySide6.QtCore import QObject, Property, Signal, Slot

DESKTOP = Path(__file__).resolve().parents[1]


def task_for(state="transferring", identity="preview-001"):
    vectors = json.loads((DESKTOP / "tests/fixtures.json").read_text(encoding="utf-8"))
    base = next(v["message"]["result"]["snapshot"] for v in vectors["valid"]
                if v["name"] == ("completed" if state == "completed" else "queued_unknown"))
    task = copy.deepcopy(base)
    task.update(transfer_id=identity, state=state, revision=7, adapter="demo")
    task["source"]["path"] = "/home/preview/datasets/research-images"
    task["destination"]["path"] = "incoming/research-images"
    task["destination"]["node_id"] = "preview-node"
    task["attempt"].update(attempt_id="preview-attempt-001", state=state)
    task["backend_evidence"] = dict(manifest=None, checksum=None, finalize=None,
        phase_timing=dict(control_prepare=None, data_connect=None, first_payload=None,
                          payload_io=None, transfer_complete_wait=None))
    if state in {"transferring", "completed", "failed"}:
        task["scan_complete"] = True
        total, moved = 2 * 1024**3, 768 * 1024**2
        done = 128 if state == "completed" else 48
        if state == "completed":
            moved = total
        task["metrics"].update(files_total=128, files_committed=done,
            files_failed=1 if state == "failed" else 0, files_skipped=0,
            bytes_total=total, bytes_committed=moved, bytes_transferred=moved,
            bytes_verified=moved, speed_bps=12 * 1024**2 if state == "transferring" else None,
            average_speed_bps=None, eta_seconds=None)
    if state == "completed":
        task["backend_evidence"].update(manifest={"status": "observed"},
            checksum={"status": "pass", "algorithm": "crc32c"}, finalize={"status": "pass"})
    if state == "failed":
        task["error"] = dict(code="permission_denied", scope="transfer",
            message="目标目录没有写入权限。请检查节点授权根目录，再重新提交。",
            retryable=False, transfer_id=identity, file_id=None, range_id=None)
        task["transfer_status"] = "fail"
        task["recovery"] = dict(eligibility="ineligible", checkpoint_id=None,
                                reason="此预览场景没有恢复点")
    return task


class FixtureRpc(QObject):
    changed = Signal()
    pageRequested = Signal(int)
    createSucceeded = Signal(str, str, int)
    taskRemoved = Signal(str)

    def __init__(self, scenario="tasks"):
        super().__init__()
        task_state = {"scanning": "scanning", "completed": "completed", "failed": "failed"}.get(scenario, "transferring")
        tasks = [] if scenario in {"offline", "empty"} else [task_for(task_state)]
        if scenario == "tasks":
            tasks += [task_for("queued", "preview-002"), task_for("completed", "preview-003")]
        self.values = dict(connected=False, apiCompatible=False, stale=scenario == "offline",
            demoAdapter=True, tasks=tasks,
            profiles=[dict(node_id="preview-node", name="研究节点 · 示例", host="node.example.invalid",
                control_port=22310, default_root_id="home", ca_path="/home/preview/config/ca.pem",
                server_name="node.example.invalid", credential_ref="file:/home/preview/config/credential.ref", auth_mode="token")],
            attempts=[], files=[], ranges=[], capabilities={}, checks={},
            agentInfo={"adapter": "demo", "api_version": "2.0.0"},
            detailStatus="设计预览：没有请求 agent 分页", detailWatermark="预览中不可用",
            message="设计预览 / 固定合成数据 / 未连接真实 agent。这里的操作不会发起传输。",
            createInFlight=False, createStatus="", createError="", removeInFlight=False,
            removeStatus="", removeError="", actionInFlight=False, actionTransferId="",
            actionStatus="", actionError="")

    @Slot()
    def refresh(self):
        self.values["message"] = "设计预览不会连接 agent；请在正式 Linux 客户端检查真实连接。"
        self.changed.emit()

    @Slot(str, str, str, bool, str, str)
    def create(self, *_):
        self.values["createError"] = "设计预览禁止创建真实任务。"
        self.changed.emit()

    @Slot(str, "QVariantMap")
    def action(self, *_):
        self.values["actionError"] = "设计预览未发送操作；没有真实成功结果。"
        self.changed.emit()

    @Slot("QVariantMap", bool)
    def saveProfile(self, *_):
        self.refresh()

    @Slot(str, str)
    def testNode(self, *_):
        self.refresh()

    @Slot()
    def demo(self):
        self.refresh()

    @Slot(str, str)
    def loadDetails(self, *_):
        self.changed.emit()

    @Slot()
    def loadNextFiles(self):
        self.changed.emit()

    @Slot()
    def loadNextRanges(self):
        self.changed.emit()

    @Slot(str, str)
    def removeTask(self, *_):
        self.refresh()

    @Slot(str)
    def requestCapabilities(self, *_):
        self.changed.emit()

    @Slot(int)
    def setPage(self, page):
        self.pageRequested.emit(page)

    def _property(key, kind, signal):
        return Property(kind, lambda self: self.values[key], notify=signal)

    for _key in ("connected", "apiCompatible", "stale", "demoAdapter", "createInFlight",
                 "removeInFlight", "actionInFlight"):
        locals()[_key] = _property(_key, bool, changed)
    for _key in ("tasks", "profiles", "attempts", "files", "ranges"):
        locals()[_key] = _property(_key, "QVariantList", changed)
    for _key in ("capabilities", "agentInfo", "checks"):
        locals()[_key] = _property(_key, "QVariantMap", changed)
    for _key in ("detailStatus", "detailWatermark", "message", "createStatus", "createError",
                 "removeStatus", "removeError", "actionTransferId", "actionStatus", "actionError"):
        locals()[_key] = _property(_key, str, changed)
    del _key, _property
