# V3-SUMMARY-CRC-AUTO-01

状态：实现与回归验收完成；4 GiB/mixed 性能收益待最终矩阵测量
日期：2026-10-23
权威工作区：深圳 `/root/projects/CPNetFlux`
输入提交：`ba8c178d8145ce5f3f4acb786438b82e8066fd90`

## 范围

让 JSON summary 的 tree hash 使用 transfer options 中配置的 CRC32C backend（默认 Auto），并记录哈希计算耗时和实际解析出的 backend。保持现有 tree hash 的输入字节序列、过滤规则和传输计时边界不变。

## 非目标

本项不复用 per-file/chunk CRC，不做 CRC combine，不修改文件清单/传输流程，不改变 checksum 默认值，不将 summary 耗时计入或排除现有 `elapsed_seconds` 的定义。大文件收益须在后续受控实验中测量。

## 验收标准

1. 软件、自动及可用时的硬件 backend 对相同目录得到完全相同的 tree hash。
2. summary 提供 `summary_tree_hash_seconds` 和 `summary_tree_hash_backend`；backend 字段表示实际使用的软件/硬件实现，不把请求的 `auto` 冒充实际实现。
3. 集成测试覆盖空文件、嵌套目录、非 ASCII 文件名和跨多个读取块的数据，并以独立 CRC32C 参考值核对字节定义。
4. 对应 smoke、相关 CTest 和完整 CTest 通过；`git diff --check` 通过。

## 预期影响文件

- `src/core/io/tree_transfer_client.cpp`
- `tools/test/run_cpnetflux_summary_crc_auto_smoke.py`
- `CMakeLists.txt`
- 本任务记录、性能路线和 `docs/ROADMAP.md`

## 实施和验收记录

实现提交前代码基线：`ba8c178d8145ce5f3f4acb786438b82e8066fd90`。tree summary 改为向 CRC32C `ChecksumComputer` 传入配置的 backend，另外写出 `summary_tree_hash_seconds` 与解析后的 `summary_tree_hash_backend`。失败时 backend 报告 `unavailable`。现有 `elapsed_seconds` 起止点、tree hash 内容拼接字节、排序和临时文件过滤保持不变。

新增集成 smoke 独立计算 CRC32C 参考值，覆盖空文件、中文嵌套路径、文本文件和超过 2 MiB 的内容。深圳 CPU 支持硬件 CRC；软件、Auto、硬件三种请求都得到 `crc32c:3a314a68`，Auto/硬件实际解析为 hardware。此次小输入 smoke 中，软件 hash 约 5.6 ms、Auto 约 1.0 ms、显式 hardware 约 1.0 ms。这只是功能验证中的单次小样本，不代表 4 GiB 或 mixed 负载的性能结论。

验证：Release build `/tmp/cpnetflux-build-fd-release-01` 成功；专项 `cpnetflux_summary_crc_auto_smoke` 通过；`CPNETFLUX_TEST_TOKEN=cpnetflux-test-token-only ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1` 通过 418/418，`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因运行环境不支持而 skip；`git diff --check` 通过。4 GiB 与 mixed 的 summary/core/wall 配对测量留在第 8 项统一性能闭环中。

变更文件：`src/core/io/tree_transfer_client.cpp`、`tools/test/run_cpnetflux_summary_crc_auto_smoke.py`、`CMakeLists.txt` 和本记录。下一项：`V3-TREE-MANIFEST-COALESCE-01`。
