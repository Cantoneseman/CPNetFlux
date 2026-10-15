"""Regression contract for the desktop reliability state machine and UI affordances."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "ui/rpc_client.cpp").read_text()
HEADER = (ROOT / "ui/rpc_client.h").read_text()
QML = (ROOT / "ui/Main.qml").read_text()

# These assertions protect the user-visible failure semantics without starting a
# long-running backend or requiring a physical display.
assert "createInFlight" in HEADER and "createStatus" in HEADER
assert "requestTimer_" in HEADER and "checkPending" in HEADER
assert "未确认 transfer_id" in CPP
assert "重复点击已忽略" in CPP
assert "lastCreateOperationId_" in CPP and "operation_id" in CPP
assert 'emit createSucceeded(transferId' in CPP
assert 'sendPostHandshake("transfer.get"' in CPP
assert "detailPageSnapshots_" in HEADER and "detailPageSnapshots_" in CPP
assert "agent unavailable" in CPP
assert "if (!connected() || !apiCompatible_ || stale_)" in CPP

# There is no transfer.delete in the current agent API. The UI must expose the
# local-only behavior and keep demo/real namespaces separate.
assert "removeTask" in HEADER and "removeTask" in CPP
assert "hiddenDemo_" in HEADER and "hiddenReal_" in HEADER
assert "hidden_tasks/demo" in CPP and "hidden_tasks/real" in CPP
assert "不会删除远端文件、目录或 agent 任务" in CPP
assert "从列表隐藏" in QML and "这不会删除远端文件、目录或 agent 任务" in QML

# No implicit navigation may make a rejected create look successful.
create_click = QML.split('objectName:"createButton"', 1)[1].split('}', 2)[0]
assert "rpc.create(" in create_click and "window.page=0" not in create_click
assert "!rpc.createInFlight" in create_click
assert "onCreateSucceeded" in QML
print("desktop creation/reconnect/detail/remove UI contract passed")
