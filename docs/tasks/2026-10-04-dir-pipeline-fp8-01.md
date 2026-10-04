# DIR-PIPELINE-FP8-01：流水线调度支持多文件并发

- 路线版本：R2026-10-04.1；固定输入：`9aab801784e24aa156e17f61ffe5dac48ab02b56`。
- 目标：在 `--control-pipeline-depth=1` 下保留单个 lookahead 候选，同时允许 `--file-parallelism` 个文件传输 worker 并行；优先支持 100 Mbps 小文件目录链路。
- 非目标：不改协议帧、resume、global scheduler、压缩/checksum 默认值；不运行深圳—上海性能实验，不修改 live root。
- 允许文件：`src/config/tree_transfer_options.cpp`、`src/core/io/tree_transfer_client.cpp`、`tests/unit/tree_transfer_options_test.cpp`、`tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py`、本任务单。
- 资源边界：lookahead 始终至多 1 个候选、仍只使用其现有双 control slot，不引入随 file parallelism 增长的候选队列；depth=1 保持 worker reuse、scheduler off、无 resume/max-files 的限制。
- 验收：解析器接受 depth=1 + file_parallelism=8；loopback 覆盖 upload/download、depth 0/1、并发度 1/8，校验业务文件 SHA-256、sidecar 与 summary 计数；相关单测及 CTest 通过。失败/取消路径不得遗留 joinable worker。
- 实现位置：深圳隔离 worktree `/tmp/cpnetflux-runs/DIR-PIPELINE-FP8-01/src`，分支 `codex/DIR-PIPELINE-FP8-01`。验收后仅提交允许文件并推送已核实的 CPNetFlux GitHub remote，回读远端 commit。
- 证据：此任务单、代码 diff、Linux build/CTest 输出；本阶段不产生跨域带宽结论。

## 验收结果（2026-10-04）

- 环境：深圳 `iZwz9bgztwf1tic26q48pjZ`，Ubuntu 22.04 / Linux 5.15，g++ 11.4.0，CMake 3.22.1；输入提交 `9aab801784e24aa156e17f61ffe5dac48ab02b56`；构建前 `/tmp` 可用约 17 GiB，复验后约 16.74 GiB。
- `cmake -S . -B build -DCPNETFLUX_BUILD_TESTS=ON`：通过。
- `cmake --build build --parallel 2`：通过。
- `ctest --test-dir build -R 'TreeTransferOptionsTest|TreePipeline' --output-on-failure`：21/21 通过。
- `ctest --test-dir build -R 'cpnetflux_tree_(upload|download|parallel|control_reuse|pipeline_sidecar|resume|changed_file|edge_cases)_smoke' --output-on-failure`：8/8 通过。sidecar smoke 覆盖 depth 0/1、并发度 1/8、双向传输、36 个文件的 hash/manifest/计数。
- Python smoke 语法检查及 `git diff --check`：通过。红灯阶段，新增 parser 单测及 depth1/fp8 loopback 均按预期失败；实现后转绿。
- 二进制 SHA-256、逐项日志、提交和远端 ref 回读记录在任务外证据包 `D:\Project\CPNetFlux-evidence\DIR-PIPELINE-FP8-01`；本仓库不存构建产物。
- 限制：未运行跨域性能实验，本回合没有吞吐提升结论；候选数固定为 1、复用已有两条 pipeline control slot。线程栈仍由平台默认值分配，未完成硬内存预算证明，因此该实现不构成旧资源契约的完整验收，也不扩展到大于 16 的并发。
