# DIR-PIPELINE-IMPL-01 执行结果

- 输入基线：`ef9c7cf938af3bb0e9595b374e4906c9da209c10`
- worktree：`/tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-01`
- 分支：`codex/DIR-PIPELINE-IMPL-01`
- live root `/root/projects/CPNetFlux` 未修改；未访问上海、未启动网络实验。

## 实现

- `TreeTransferOptions` 新增 `controlPipelineDepth`，默认 `0`；解析 `--control-pipeline-depth 0|1`，缺值、负数、溢出、>1 均返回 invalid argument。
- depth=1 在 parser 阶段限定为 `--control-reuse worker`、scheduler off、`fileParallelism=1`、非 resume；同时拒绝 `--max-files` 以避免候选预留改变 max-files 语义。
- tree client 增加单 worker pipeline：当前文件进入数据客户端后启动至多一个候选线程；候选在独立 `ControlClient` 上完成元数据检查、`EPSV` 和 `STOR`/`RETR`，不建 data socket。当前数据和 226 完成后，候选控制连接用于下一文件。
- 候选只接受 Pending 记录，使用路径校验、upload 本地 stat 或 download 远端 SIZE/MDTM，并复用 `acquireTransferSlot`。候选失败标记该记录 Failed、关闭其控制连接并停止；当前失败时 preparation thread 先 join，随后析构回收控制连接/服务端 passive reservation。
- 默认 depth=0 继续使用原 scheduler/worker 路径；新增 JSON `control_pipeline_depth` 仅在 depth=1 时输出。wire、manifest 格式、resume/checksum、scheduler/compression、TLS/server 均未修改。
- `tests/unit/tree_pipeline_state_test.cpp` 是不依赖网络的候选 reservation/swap/failure cleanup/depth-zero 状态机测试；选项测试覆盖默认、1、>1 和组合拒绝。

## 验证命令

- `cmake -S /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-01 -B /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-01/build -G Ninja -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_ENABLE_TLS=ON`：退出 0。
- `cmake --build /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-01/build --parallel 2`：退出 0，110/110。
- `ctest --test-dir /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-01/build --output-on-failure -R 'TreePipelineStateTest|TreeTransferOptionsTest|TreeManifestTest|TreeScanTest'`：退出 0，20/20 通过。
- `git diff --check`：退出 0。
- 变更路径门禁：相对基线仅 `CMakeLists.txt`、两个 tree options 文件、`tree_transfer_client.cpp`、两个 focused test 文件；均在任务白名单内。

## 未运行与限制

- 未运行 tree upload/download smoke、depth=1 实际传输、hash/manifest on/off 对照、完整 CTest、性能 A/B 或跨域实验；任务禁止网络实验，且深圳有长期 GridFTP 服务进程。
- 单元状态机不证明真实 server passive listener 行为。候选 preparation 使用 join 等待阻塞控制操作，失败后由 RAII 关闭控制连接；当前实现没有独立的 socket read deadline/cancel API，若服务端永不回复，停止会等待该控制操作结束。这是后续 04 复核的已知限制，不宣称生产 readiness 或性能收益。
- 由于 depth=1 只允许 fresh、单 worker、worker control reuse，global scheduler、resume 和默认 depth=0 语义未改变。

## 提交

提交 SHA、origin push 及远端 SHA 回读在完成后补记。
