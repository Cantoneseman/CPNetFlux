"""Regression contract for the desktop run-start state machine and UI affordances."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "ui/rpc_client.cpp").read_text()
HEADER = (ROOT / "ui/rpc_client.h").read_text()
QML = (ROOT / "ui/Main.qml").read_text()

assert "createInFlight" in HEADER and "createStatus" in HEADER
assert "requestTimer_" in HEADER and "checkPending" in HEADER
assert "未确认 transfer_id" in CPP
assert "重复点击已忽略" in CPP
assert "lastCreateOperationId_" in CPP and "operation_id" in CPP
assert 'emit createSucceeded(transferId' in CPP
assert 'sendPostHandshake("transfer.get"' in CPP
assert 'sendRequest("transfer.start"' in CPP
assert 'actionInFlight_' in CPP and 'expected_revision' in CPP
assert 'capability_mismatch' in CPP and 'queued 任务' in CPP
assert "detailPageSnapshots_" in HEADER and "detailPageSnapshots_" in CPP
assert "agent unavailable" in CPP
assert "if (!connected() || !apiCompatible_ || stale_)" in CPP
assert "removeTask" in HEADER and "removeTask" in CPP
assert "hiddenDemo_" in HEADER and "hiddenReal_" in HEADER
assert "hidden_tasks/demo" in CPP and "hidden_tasks/real" in CPP
assert "不会删除远端文件、目录或 agent 任务" in CPP
assert "从列表隐藏" in QML and "这不会删除远端文件、目录或 agent 任务" in QML
create_click = QML.split('objectName:"createButton"', 1)[1].split('}', 2)[0]
assert "rpc.create(" in create_click and "window.page=0" not in create_click
assert "!rpc.createInFlight" in create_click
assert "onCreateSucceeded" in QML
assert "启动中…" in QML and 'rpc.action("transfer.start"' in QML
print("desktop run-start UI contract passed")
