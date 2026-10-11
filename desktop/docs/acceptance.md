# Agent API acceptance and staged tests

## AGENT-API-01 (00, completed in this task)

Frozen API 2.0.0 schema and offline semantic checks:

```bash
python3 -B desktop/tests/test_contract.py
```

The Python demo agent implements `agent.getInfo`, the schema-validated API 2.0 request surface, bounded demo-only attempt/file/range snapshot pages, persisted attempt history, and explicit effective-policy state. Runtime checks:

```bash
python3 -B desktop/tests/test_agent.py
```

Demo entities and effective policy are synthetic. They never prove real file I/O, TLS, checksums or network capability. `node.capabilities` stays empty until a real authenticated probe is implemented. The contract vectors do not claim live backend behavior.

## Desktop next milestone

1. Make Qt perform `agent.getInfo` before other calls and refuse an incompatible API version.
2. Integrate `attempt.list/file.list/range.list` pages in detail views; bind cursors to transfer/attempt/file generation and preserve snapshot watermark during event replay.
3. Display `adapter=demo` as simulation and never present demo effective policy as real transport evidence.
4. Add UI/agent tests for version mismatch, page loading/error/expiry, event reconnect and duplicate operations. Run Python tests, CMake build and complete desktop CTest.
5. Keep real transfer methods unsupported until secure OS secret-store lookup, safe credential delivery, and structured core evidence are available. Do not pass secrets through argv/env/RPC/log or invent temporary plaintext storage.

Real core integration, credential injection, systemd installation, cross-host transfer tests and performance experiments require separate tasks. UI smoke tests do not validate backend transport semantics.
