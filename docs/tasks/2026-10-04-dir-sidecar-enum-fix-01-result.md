# DIR-SIDECAR-ENUM-FIX-01 实现回执

- 状态：待 04 独立验收；本回执只证明 sidecar 枚举修复的实现和本地 loopback 正确性。
- 固定输入：DEV-02 7d7b77c9238b1c20b0f3281f238b95051948c8cc。
- 实现提交：e7f0ee3ba317d8dc45f997c1f04ec94570013051，分支 codex/DIR-SIDECAR-ENUM-FIX-01；已 push 至 CPNetFlux GitHub，ls-remote 回读 SHA 与本地一致。
- 工作区：深圳隔离 worktree /tmp/cpnetflux-runs/DIR-SIDECAR-ENUM-FIX-01/src；未修改 /root/projects/CPNetFlux live root、DEV-02/QA worktree 或上海。

## 修改结果

树扫描器和服务端 LIST/NLST 现在只把能成功解析、且其中 output/target path 与 sidecar 文件名严格对应的 CPNetFlux transfer/download manifest 识别为内部状态文件。普通近似名、无效 manifest，或内容指向其他 payload 的文件仍作为用户文件处理；没有按 .cpnetflux. 子串做过滤。

新增 TreeScanTest.ExcludesValidatedSidecarsAndKeepsUserFiles，同时检查有效上传/下载 sidecar 被排除、无效与路径不匹配的 manifest 样式用户文件以及 notes.cpnetflux.user.txt 被保留。新增并注册 cpnetflux_tree_pipeline_sidecar_smoke：对 depth 0 和 1 分别执行上传、下载，以同一份 4 文件小树验证业务文件集合、逐文件 SHA-256、服务端/下载端原始文件集合，以及 file_count、data_transfer_count 均不被 sidecar 膨胀。

## 验证

构建环境为深圳 Ubuntu 22.04.5、GCC 11.4、CMake 3.22.1，Release，TLS ON，io_uring OFF。每个阶段均在隔离目录执行，loopback payload 由 TemporaryDirectory 自动清理。

- CMake Release 配置：退出码 0。
- cmake --build ... --target cpnetflux_unit_tests --parallel 1：退出码 0。
- ctest --test-dir ... -R TreeScanTest --output-on-failure：4/4 通过；修改测试用例再次构建并复跑仍为 4/4。
- python3 -m py_compile tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py：退出码 0。
- ctest --test-dir ... -R cpnetflux_tree_pipeline_sidecar_smoke --output-on-failure：1/1 通过；实际覆盖 depth 0/1 × upload/download。
- 全量 build：退出码 0；全部 220 项 CTest：220/220 通过，io_uring 可选测试 1 项 skipped。全量 CTest 运行时以临时随机值注入 CPNETFLUX_TEST_TOKEN；初次未注入该测试变量时，token-auth 与 event-log 两项失败，随后定向 2/2 通过且最终全套 220 项全绿。随机值未写入文件或日志。
- 最终全量证据：/tmp/cpnetflux-runs/DIR-SIDECAR-ENUM-FIX-01/evidence/configure.log、build.log、full-ctest.log；CMake 的原始测试记录在 build 目录 Testing/Temporary/LastTest.log。
- git -c core.whitespace=cr-at-eol diff --check：提交前通过；提交仅包含任务单及其白名单源码、测试文件。

## 结果边界与未完成项

sidecar 不再扩张目录业务集合的功能正确性通过了深圳 loopback 回归；这不是性能结果，未与 GridFTP 对比，也未进行 100 Mbps 跨域传输。

DEV-02 QA 已确认 pending command reject/timeout、listener disconnect、candidate cancel/cleanup 仍为 NOT_RUN。因此本任务仅消除 sidecar blocker，目录命令流水线总体仍 BLOCKED，不得启动性能或跨域实验。后续由 04 固定本提交独立验收；之后再单独补齐上述故障门禁。
