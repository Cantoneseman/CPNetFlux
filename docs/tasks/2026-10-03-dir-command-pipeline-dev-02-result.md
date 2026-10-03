# DIR-COMMAND-PIPELINE-DEV-02 实现回执

## 状态与范围

本回执对应固定输入 `0306493ef0e2b6b1ddb75e0fb6ee4f6208c308e5`，工作树为深圳隔离 worktree `/tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-02/src`，分支 `codex/DIR-COMMAND-PIPELINE-DEV-02`。live root `/root/projects/CPNetFlux` 未修改；没有访问上海、没有云端跨域实验，也没有提交构建产物或凭据。

本切片只修复 pipeline candidate 的进程内身份校验：`PreparedTransfer` 保存 manifest index、relative path、upload/download direction 与 `PipelineControlSlot::generation` 快照，并在候选准备及 upload/download handoff 前复核。没有新增协议字段、summary/event 字段、transfer ID 或持久化状态。

## 修改文件

- `include/cpnetflux/core/io/tree_pipeline_identity.h`：新增不可序列化的进程内身份值和纯匹配函数。
- `src/core/io/tree_transfer_client.cpp`：`PreparedTransfer` 改为持有身份快照；`validatePreparedCandidate` 在 `prepareUploadCandidate`、`prepareDownloadCandidate` 及两条实际 handoff 路径检查 slot/control 指针、ready、manifest index/path、Pending 状态、方向和 generation；候选失败路径使用快照 index 更新失败记录。
- `tests/unit/tree_pipeline_identity_test.cpp`：覆盖匹配、index/path/direction/generation 改变的拒绝以及最大 generation 的 download identity。
- `CMakeLists.txt`：注册上述窄单测。

原有 depth=0 分支未改；本切片未修改 file client、control protocol、manifest/checkpoint/resume/checksum、scheduler/compression、IO backend 或服务端。

## 静态验证

- `git rev-parse HEAD`：`0306493ef0e2b6b1ddb75e0fb6ee4f6208c308e5`（固定输入，未改写）。
- `git status --short --branch`：分支正确；提交前仅列出上述源码/CMake/测试与本回执的未提交修改，index 为空。
- `git diff --check`：退出码 0。
- `grep` 核对 `PreparedTransfer` 原 `prepared->index`/`candidate->index` 旁路已移除；实际校验调用位于 `tree_transfer_client.cpp:1224-1244,1480-1490,1539-1549,1637-1645,1829-1837,2813-2819`（以最终文件行号为准）。
- 未修改 live root、旧 DEV-01/QA worktree 或 Git index。

## 构建与测试

证据目录：`/tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-02/evidence`。

1. `cmake -S /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-02/src -B /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-02/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF`：退出码 0，日志 `configure-targeted.log`。
2. `cmake --build ... --target cpnetflux_unit_tests --parallel 2`：退出码 0，日志 `build-targeted.log`。
3. `ctest --test-dir ... --output-on-failure -R 'TreePipelineCandidateIdentityTest|TreePipelineStateTest'`：退出码 0，6/6 通过，日志 `ctest-targeted.log`。
4. `cmake --build ... --parallel 2`：退出码 0，日志 `build-full.log`。
5. 使用仅存在于该 ctest 进程的临时 `CPNETFLUX_TEST_TOKEN` 运行 `ctest --test-dir ... --output-on-failure`：退出码 0，218/218 通过；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因显式 `CPNETFLUX_ENABLE_IO_URING=OFF` 跳过。日志 `ctest-full.log`。

## Loopback 结果与 correctness blocker

使用已构建 CPNetFlux server/client、loopback、`connections=1`、`file-parallelism=1`、`scheduler=off`、`control-reuse=worker`，分别运行 depth=0 和 depth=1 upload/download；没有跨域或性能结论。独立证据为 `loopback-paired-summary.log`，另有完整诊断 `loopback-sidecar.log`。

- upload depth=0：客户端返回 0；3 个业务文件 hash 与源一致；服务器目录共 6 个文件，其中每个业务文件旁有 `.cpnetflux.manifest`。
- upload depth=1：客户端返回 0；3 个业务文件 hash 与源一致；服务器目录同样为 6 个文件。
- download depth=0：客户端返回 0；过滤内部 sidecar 后 3 个业务文件 hash 与源一致，但原始目标目录 12 个文件，tree manifest 有 6 行。
- download depth=1：客户端返回 0；过滤内部 sidecar 后 3 个业务文件 hash 与源一致，但原始目标目录 12 个文件，tree manifest 有 6 行。

因此业务文件内容在该小 fixture 上一致，但完整文件集合、`file_count/data_transfer_count` 和 tree manifest 不等价：服务端为业务文件生成的 `.cpnetflux.manifest` 被 LIST/NLST 枚举，下载又为这些 sidecar 生成 download sidecar，形成递归污染。该问题与已知 TREE-SIDECAR-01 correctness 任务相同，本任务没有越界修改它；故 DEV-02 不能宣称 loopback correctness gate 通过，状态为 **BLOCKED（sidecar 枚举）**。

## 未运行/剩余限制

以下没有被本切片验证，均保持 NOT_RUN：

- pipeline pending command reject/timeout（当前 ControlClient 的 command/readReply 路径没有本任务新增的专属 deadline 验收）；
- opt-in listener control disconnect、当前文件失败时的 candidate cancel/资源清理；
- 线程启动失败、网络断连、TLS/auth reject 后的深度 1 资源恢复；
- resume/changed-file、manifest save failure 下 depth 1 的端到端状态等价；
- 真实跨域、性能 A/B、连接数/命令时序 profile。

generation 快照和身份单测可供 04 独立复核；在 sidecar 过滤修复及上述故障注入门禁完成前，不建议进入云端性能验证。
