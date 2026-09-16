# Phase 1 Global Scheduler MVP 验收审计

> 目标：把现有 Phase 1 并发能力、tree transfer、perf 脚本和深圳/上海验收闭环收口成可复查交付。

## 适用范围

- 方案 B：Global Scheduler 先作为独立核心库的方向保留，但当前分支的验收重点是 tree transfer 集成、raw-only 对照和双机验证。
- Phase 1 不做 daemon。
- Phase 1 不做 Prometheus / HTTP metrics，只做 CSV + JSONL。
- Phase 1 不做跨文件数据连接复用。
- Phase 1 不依赖 NIC name、NUMA、CPU pinning、RSS 或 PCIe topology。
- Phase 1 只做无损压缩策略；压缩不能阻塞 raw path。
- 完成事实源只认 manifest / verified_chunks / tree manifest。

## 当前模块落点

- `src/core/io/tree_transfer_client.cpp`
- `include/cpnetflux/config/tree_transfer_options.h`
- `src/config/tree_transfer_options.cpp`
- `tools/perf/run_gridftp_tree_private_matrix.py`
- `tools/perf/run_gridftp_private_matrix.py`
- `tools/perf/sync_remote.sh`
- `tools/experiments/beta_matrix/runner.py`

## 验收审计

| 项目 | 状态 | 证据/命令 |
| --- | --- | --- |
| 本地构建与测试 | 待验收 | `cmake --build build` / `ctest --test-dir build --output-on-failure` |
| 深圳构建与测试 | 待验收 | `ssh cpnetflux-beta-shenzhen "cd /root/projects/CPNetFlux-Beta && cmake --build build && ctest --test-dir build --output-on-failure"` |
| 上海构建与测试 | 待验收 | `ssh cpnetflux-beta-shanghai "cd /root/projects/CPNetFlux-Beta && cmake --build build && ctest --test-dir build --output-on-failure"` |
| tree control reuse smoke | 待验收 | `ctest --test-dir build -R cpnetflux_tree_control_reuse_smoke --output-on-failure` |
| tree private matrix | 待验收 | `python3 tools/perf/run_gridftp_tree_private_matrix.py ...` |
| file private matrix | 待验收 | `python3 tools/perf/run_gridftp_private_matrix.py ...` |
| raw-only GridFTP 对照 | 待验收/可能 blocked | 先跑 `tools/experiments/beta_matrix/runner.py --dry-run`，真实 GridFTP 需要 GCT/GSI preflight |
| 深圳同步到上海 | 待验收 | `tools/perf/sync_remote.sh --host root@47.116.174.181 --source /root/projects/CPNetFlux-Beta --target /root/projects/CPNetFlux-Beta` |

## 不得破坏的语义

- `--control-reuse off|worker` 默认仍是 `off`。
- `--planner-preset` 只作为计划/实验标签，不改变完成事实源。
- `--resume` 仍以 manifest / verified_chunks / tree manifest 为恢复事实源。
- `tree transfer` 的 changed-file fail-safe 不能被绕过。
- `GridFTP` 只作为对照基线，不可用 scp / rsync 替代。

## 深圳恢复执行命令

如果深圳主测中断，先恢复构建与 CTest：

```bash
ssh cpnetflux-beta-shenzhen \
  "cd /root/projects/CPNetFlux-Beta && cmake --build build && ctest --test-dir build --output-on-failure"
```

如果需要通过密码路径执行，再走统一 auth 包装：

```bash
python3 tools/release/remote_auth.py \
  --remote root@47.116.174.181 \
  --repo-root "D:/Project/CPNetFlux Beta" \
  --sshpass-prefix \
  -- ssh -o StrictHostKeyChecking=no root@47.116.174.181 'cd /root/projects/CPNetFlux-Beta && cmake --build build && ctest --test-dir build --output-on-failure'
```

## 交付物

- 树传输并发/复用验收结果
- 深圳/上海 build + CTest 记录
- tree/file 私网矩阵 CSV + JSONL
- raw-only GridFTP 对照计划或 blocked 说明
- 最终验收结论
