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

# Presentation contract: the user-facing view must explain real agent stages,
# keep unknown evidence explicit, and hide protocol-level fields by default.
for marker in ["正在扫描目录", "等待远端准备", "阶段进度", "扫描目录", "远端准备", "传输数据", "校验清单", "提交完成", "待处理", "正在获取文件清单", "实时后端事件", "完整性核验", "展开技术详情", "收起技术详情", "技术详情（用于排查，不影响正常使用）"]:
    assert marker in QML, marker
assert "indeterminate:" not in QML
assert "CPNetFlux V3（动态目录/大文件分段）" in QML
assert "engineLabel" in QML and "engineDetail" in QML
assert "目录浏览暂不可用" in QML
assert "尚未检查" in QML
assert "files_discovered" in QML
assert "backend_evidence" in QML and "integrityText" in QML
assert "phaseStatus" in QML and "stateSummary" in QML
print("desktop run-start and state-presentation UI contract passed")
