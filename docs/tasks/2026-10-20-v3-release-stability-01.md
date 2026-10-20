# V3-RELEASE-STABILITY-01

Date: 2026-10-20 (Asia/Shanghai)
Status: slices A and B complete; parent release-stability task remains in_progress
Owner: current commander, single serial developer
Input commit: bb6cb2022e813c274387d915e3024a4546ac0549
Branch: codex/V3-RELEASE-STABILITY-01

## 易懂版问题

当前已经验证“传完后再运行 resume”，但这不能证明用户在传输到一半时关掉客户端，之后还能接着传。本阶段要真实打断一次正在进行的 V3 上传和下载，确认已经写好的检查点没有丢失，重新运行 V3 resume 后能补齐剩余数据且最终内容完全正确。

## 本阶段范围（slice A）

- 新增可重复的 V3 loopback smoke，分别覆盖上传和下载。
- 传输期间等待接收端 checkpoint 显示至少一个已校验 chunk、且尚未提交，再向客户端发送 SIGTERM。
- 用相同 V3 客户端参数和 resume 继续；断言续传成功、最终文件 SHA-256/目录 SHA-256 一致，结果明确记录 V3 persistent executor。
- 将场景注册到 CTest；先运行红测。只有红测证明产品行为失败时，才做最小生产代码修复。
- 同一固定提交在深圳、上海构建并运行定向/完整 CTest，核对关键二进制 SHA-256；两端构建通过且哈希一致后，再做两台服务器间的实际 V3 上传/下载 smoke。

## 非目标

- 本阶段只完成发布稳定性计划中的“客户端中断后续传”一项；服务重启、传输超时、损坏 checkpoint、目标冲突和重复提交仍需后续分别验收。
- 不改 V3 协议或 checkpoint schema，不引入 V1/V2 runtime，不改正式服务配置/进程。
- 不进行吞吐结论、生产 readiness、物理 NIC、local-bind 或不同物理链路验证。
- 不触碰历史 GridFlux-Beta、CPSS(DCC)、其他 worktree、凭据和构建产物。

## 验收标准

1. 新测试能观察到真实的非空 verified_chunks checkpoint 后再中断客户端；若只在空 checkpoint 或传完后才退出，测试失败。
2. 上传和下载的初次客户端都以 SIGTERM 中断；随后 V3 resume 成功，最终源/目的文件和目录 SHA-256 一致。
3. 测试记录初次进程退出码、checkpoint/日志和续传 JSON 摘要，确认执行器/transport 为 V3 persistent tree；若当前摘要没有实际发送/跳过字节指标，则记录可核验的 checkpoint 行为，不虚构指标。
4. 新 CTest 定向通过；完整 CTest 在两端记录通过数、失败项和可选跳过项。
5. 两端从同一已提交源码构建，记录配置与 server/upload/download 二进制 SHA-256；哈希一致后才进行真实跨机传输，记录双向结果和 payload SHA-256。
6. 任务文件及 evidence JSON 记录输入/实现提交、命令、退出码、摘要路径、源归档 SHA、资源/服务状态、push 状态和下一步。

## 预计受影响文件

- tools/test/run_v3_interrupted_resume_smoke.py（新增）
- CMakeLists.txt（新增 CTest 入口）
- docs/coordination/BOARD.md
- docs/tasks/2026-10-20-v3-release-stability-01.md
- docs/tasks/2026-10-20-v3-release-stability-01-evidence.json
- 只有红测定位出产品问题时，才增加必要的 V3 实现文件，并在验收记录中解释原因。

## 验收命令草案

    python3 -m py_compile tools/test/run_v3_interrupted_resume_smoke.py
    python3 tools/test/run_v3_interrupted_resume_smoke.py --build-dir <build-dir>
    ctest --test-dir <build-dir> -R cpnetflux_v3_interrupted_resume_smoke --output-on-failure
    CPNETFLUX_TEST_TOKEN=<本机临时测试值> ctest --test-dir <build-dir> --output-on-failure -j2

深圳与上海使用同一 Release/TLS/io_uring build configuration。跨机传输仅在两端构建成功且关键二进制 SHA-256 一致后运行。

## 下一步交接

slice A 已通过。下一子阶段优先把“传输中服务端重启后 V3 是否能从持久 checkpoint 继续”做成单独故障注入门禁；其余稳定性风险保持 open，不标记为 release-ready.

## 实现与验收结果（2026-10-20）

本阶段新增 tools/test/run_v3_interrupted_resume_smoke.py，并注册 CTest cpnetflux_v3_interrupted_resume_smoke。测试在传输尚未结束时，观察到接收端至少 16 个已校验 chunk 后对客户端发送 SIGTERM，再用 V3 resume 接续；上传和下载都最终提交，跳过已完成的 16 MiB，并核对 512 MiB 源/目的内容。没有发现生产逻辑缺陷，因此未改生产 C++。

实现提交 00adb9074268cc5ec0b798c936c7ce93f56e39db 已推送到 origin/codex/V3-RELEASE-STABILITY-01，推送后 git ls-remote 回读 SHA 一致。源码从该提交生成同一归档，两端归档 SHA-256 均为 e9834a97f00ccd07849f579d2f46389fcda963ac753e08efaeffa526bed440a0。

两端均使用 Release、TLS ON、io_uring ON、测试 ON、桌面 OFF、跨域 CTest OFF 配置；GNU 11.4 / CMake 3.22.1，构建退出码均为 0。定向 CTest cpnetflux_v3_interrupted_resume_smoke 两端均 1/1 通过。完整 CTest：深圳 394/394 通过；上海首次完整运行有一个既有 cpnetflux_reconnect_smoke 失败（其余 393 项通过），单独重跑该项 1/1 通过，随后完整重跑 394/394 通过。现有证据无法确定首次失败原因，故记录为一次未复现的失败，不归因于本次改动。日志保存在两端各自 /tmp/cpnetflux-runs/V3-RELEASE-STABILITY-01/ 下，索引见 evidence JSON。

两端构建的跨机测试 server、上传客户端、下载客户端 SHA-256 完全一致；随后由深圳 V3 客户端连接上海的临时 V3 测试服务，分别实测上传和下载，并在每个方向重置第 2 条数据连接。两个方向均由 V3 persistent-tree 执行器恢复成功，未降级；3 个文件共 67,230,715 字节，实际树 SHA-256 与预期同为 5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092。正式服务全程保持 active、未重启；临时服务结束后清理了测试 .bin 大载荷，保留 matrix、摘要、client/server/proxy 日志。该跨云测试证明的是深圳客户端到上海临时 V3 服务的上传/下载故障恢复，不等同生产 readiness，也未覆盖反向方向、服务重启或物理链路。

下一子阶段：服务端在传输中重启后，V3 是否能读取持久 checkpoint 并安全继续。之后再分别验证超时、损坏 checkpoint、目标冲突、重复提交；这些尚未验收。


## Slice B：服务端重启后续传（计划，2026-10-20）

计划输入提交：3c4f3627c452d9dcc70de667d51e3c31c33d3b5b。

### 范围

- 新增同一个可复现 smoke，覆盖 V3 目录上传和下载。
- 传输 512 MiB 文件期间，先确认已持久化至少一个已校验 chunk 且尚未完成；随后只停止本任务启动的临时 CPNetFlux 服务进程。
- 确认初次传输因服务端退出而失败；检查检查点仍在磁盘且 verified_chunks 未丢失。
- 在相同 root、控制/数据端口和源码版本上重启临时服务，再以同一 transfer 输入执行 V3 resume。
- 上传、下载最终均提交；断点跳过字节为正且小于文件总长；最终文件和目录 SHA-256 正确；summary 明确 V3 persistent-tree 且无 fallback。
- 将 loopback 场景注册为 CTest；同一已提交源码在深圳、上海构建并运行定向和全量 CTest。关键 server/client 二进制哈希一致后，用深圳客户端与上海临时 V3 服务再实际执行上传、下载服务重启恢复。

### 非目标

- 不停止、重启或修改两端正式 cpnetflux-demo.service，不访问其数据目录或认证文件。
- 不修改 wire protocol、manifest/checkpoint schema；只有复现出代码错误时才修改生产实现。
- 不做超时、损坏 checkpoint、目标冲突、重复提交、性能、生产 readiness、物理 NIC 或不同物理链路验收。

### 验收标准

1. 上传和下载在服务端停止前都观察到 verified_chunks 大于 0 且小于总 chunk 数；服务器退出后初次客户端非零结束，不能靠测试脚本终止客户端来伪造服务故障。
2. 服务退出后检查点仍存在、状态未 committed、已校验 chunk 集合保持；重启相同服务后 resume 完成。
3. 最终 summary 为 result=pass、actual_transport=v3、actual_mode=persistent_tree、fallback_reason 为空，resume_skipped_bytes 在零和文件总长之间。
4. 源、目的文件 SHA-256 和目录 SHA-256 完全一致；server 两代进程、客户端日志及中间检查点摘要写入 evidence JSON / 外部证据目录。
5. 两端新 CTest 定向通过，完整 CTest 结果和失败重跑如实记录；同提交源码归档 SHA 与 server/upload/download 二进制 SHA 一致。
6. 双端同步构建及哈希核对完成之后，才执行深圳→上海临时 V3 服务的上传/下载实测；正式服务不变，记录两端资源和遗留进程/监听端口状态。

### 预计受影响文件

- tools/test/run_v3_service_restart_resume_smoke.py（新增）
- CMakeLists.txt（新增 CTest 项）
- docs/coordination/BOARD.md
- docs/tasks/2026-10-20-v3-release-stability-01.md
- docs/tasks/2026-10-20-v3-release-stability-01-evidence.json
- 生产 V3 文件仅在行为测试确认产品缺陷时修改。


## Slice B 执行与验收结果（2026-10-20）

本阶段解决的问题：服务在传输中退出时，客户端会报错；现在验证 CPNetFlux 能否保住已校验的数据进度，并在同一服务目录重启后只补传剩余部分。实现为一个故障注入 smoke 和对应 CTest，没有发现产品逻辑错误，因此未改生产 C++、协议或 checkpoint 格式。

新增 tools/test/run_v3_service_restart_resume_smoke.py，并注册 cpnetflux_v3_service_restart_resume_smoke。512 MiB 上传和下载分别先观察到 16 个已校验 chunk，再只停止本轮临时服务。客户端因服务退出以非零状态结束；恢复同一个临时服务和 root 后，V3 persistent-tree resume 均提交成功、没有 fallback。循环回归固定 512 MiB；额外 64 MiB 开发 smoke 也通过。

代码提交 3c4ec5ba6c05a355134a42cf10b47a7b48d20e81 加入测试；实际跨云暴露出目录摘要校验脚本的路径参数错误后，修正并提交 e56c631d4c9a4382cced7acd36613e50867f3408。两次提交均推送至 origin/codex/V3-RELEASE-STABILITY-01，并由 git ls-remote 回读相同 SHA。修正后的源码归档在深圳和上海 SHA-256 均为 6886bb1fc455b0c552e5405db6289153aa44d704b4220e50e29b005f90ecf004。

两端均从 e56c631d4c9a4382cced7acd36613e50867f3408 构建，Release、TLS ON、io_uring ON、测试 ON、桌面 OFF、跨域 CTest OFF；GNU 11.4 / CMake 3.22.1。服务端、上传客户端、下载客户端 SHA-256 在两端相同：
- server：e952ea928fd6c7753df6cbe4b558e829232df40b481d0f36cbbe4a860827f412
- upload：19d1bcd167e7e12ed9f2d738b624d2294b2d9a12b26e1b74f598fbd11080e14c
- download：7e488ecd507aab737b5c29afb4ae9f59503203587fa7d32f0e5479ba23bd3969

新 CTest 两端均 1/1 通过。设置 smoke 所需的一次性 CPNETFLUX_TEST_TOKEN 后，同一提交完整 CTest 串行结果为深圳 395/395、上海 395/395，退出码均为 0。并行全量运行曾让深圳已有 cpnetflux_reconnect_smoke 间歇报 duplicate persistent range；该项单独重跑通过，串行全量也通过，因此最终验收使用串行结果。没有把无 token 时两项依赖该变量的失败计为产品故障。

在两端关键二进制哈希一致之后，深圳 V3 客户端连接上海临时 V3 服务实际传输 512 MiB 文件：上传和下载各经历一次服务端进程停止、客户端失败退出、服务重启、resume 成功。两个方向均报告 actual_transport=v3、actual_mode=persistent_tree、fallback_reason 为空。上传 resume 跳过 16 MiB；下载 resume 跳过 41 MiB。源文件 SHA-256 和两端最终目标文件 SHA-256 均为 ad2f2f84c6cc1fe087ca4c4ccf19012aa50491eefa1873fcace327c8d33c6d63；源/目标目录 SHA-256 均为 3e6b8acd5fa38834adb83e70a71eb48108a6e6c6b7c15dd22f1053e653be5af8。下载客户端停止时 checkpoint 状态被记录为 failed，但 41 个已校验 chunk 仍保留，后续 resume 成功提交。

跨云汇总和全部 server/client 日志、checkpoint、SHA 结果保存在深圳 /tmp/cpnetflux-runs/V3-RELEASE-STABILITY-01/slice-b/crosscloud-evidence/20261020T030611-3020825/；上海测试目录为 /tmp/cpnetflux-runs/V3-RELEASE-STABILITY-01/slice-b/crosscloud-restart-20261020-02/。第一次跨云调试运行在文件恢复后发现校验脚本错误，未作为验收；修复后的独立新目录完整通过。完成后已删除源、上传和下载 .bin payload、临时 server/proxy 进程和测试监听端口；两端正式 cpnetflux-demo.service 始终 active，最终仅正式端口 2121 继续监听。保留日志、摘要和 manifest 供审查。

本阶段只证明当前两台云服务器、此配置和 V3 持久检查点上的服务重启恢复；不等于生产 readiness，不覆盖超时、损坏 checkpoint、目标冲突、重复提交、物理 NIC 或不同物理链路。

## Slice C：传输静默超时与续传（计划，2026-10-20）

状态：planned；计划输入提交 9173e2528b1ee14db96355c966611be050b129d8。

实际问题：对端仍连着但不再传数据时，用户需要确认传输会在设定时间内明确失败，而不是无限等待；恢复连接后也应能从已保存进度继续。

验收计划：用 loopback 可控代理在 V3 上传、下载已有非空 checkpoint 后停止转发数据；验证客户端在 --transfer-timeout-ms 约定范围内非零退出并报告明确超时，不切换旧路径；检查已校验 chunk 保留；解除静默后从相同 V3 transfer 身份 resume，最终文件和目录 SHA-256 一致。先定向和完整 CTest，再按同提交双端构建及二进制哈希门禁，在深圳→上海临时 V3 服务重复实际场景。Slice C 不修改损坏 checkpoint、目标冲突或重复提交场景，这些仍需后续分别计划；物理链路验证继续暂缓。

### Slice C 执行范围、非目标与验收

范围：为测试代理增加只作用于测试链路的数据静默开关；扩展 V3 上传/下载 smoke，在已有非空检查点后暂停数据转发，记录代理暂停事件、检查点、客户端计时与错误，然后开放转发并使用同一 V3 transfer 输入续传。先验证代理测试，再跑端到端 CTest。若端到端红测暴露产品缺陷，才最小修复 V3 代码。随后固定源码提交，在深圳、上海构建、运行定向和完整 CTest，校验关键二进制 SHA-256 一致后，才运行深圳客户端到上海临时 V3 服务的真实上传/下载。

非目标：不更改正式服务、认证配置、协议或 checkpoint schema；不触碰 GridFlux-Beta、CPSS(DCC)、其他 worktree、凭据或历史大 payload；不验证损坏 checkpoint、目标冲突、重复提交、性能、生产 readiness、物理 NIC/local-bind/不同物理链路。

验收标准：
1. 上传和下载均记录数据代理确实进入静默，且接收端/下载端已有 verified_chunks 大于 0 且小于总 chunk 数。
2. 静默期间客户端在配置的 --transfer-timeout-ms 上界内以非零状态退出，并在日志/摘要中可识别为超时；执行路径保持 V3，无 fallback。
3. 静默后 checkpoint 仍包含原有已校验 chunk 且未提交；打开同一代理继续转发后，用相同 transfer identity resume 成功。
4. 最终文件与目录 SHA-256 一致，V3 persistent-tree 摘要无 fallback。
5. 新定向 CTest 和两端同提交完整 CTest 通过；记录重试/失败项。深圳与上海源码归档 SHA 和 server/upload/download 二进制 SHA 一致后，才做跨云两方向场景。
6. 记录资源/正式服务状态、实现与证据提交、GitHub push 及远端 SHA 回读；清理测试 payload/临时服务/代理和监听端口，保留日志与 checkpoint 摘要。

预计文件：tools/test/cpnetflux_fault_proxy.py、tools/test/test_cpnetflux_fault_proxy.py、新增 tools/test/run_v3_timeout_resume_smoke.py、CMakeLists.txt、本任务 Markdown 与 evidence JSON、docs/coordination/BOARD.md；仅产品红测证实时才改生产 V3 文件。

验收命令：python3 -m py_compile tools/test/cpnetflux_fault_proxy.py tools/test/run_v3_timeout_resume_smoke.py；python3 -m unittest tools/test/test_cpnetflux_fault_proxy.py；python3 tools/test/run_v3_timeout_resume_smoke.py --build-dir BUILD_DIR --file-size-mib SIZE_MIB；ctest --test-dir BUILD_DIR -R cpnetflux_v3_timeout_resume_smoke --output-on-failure；最后两端串行 CPNETFLUX_TEST_TOKEN=temporary-test-value ctest --test-dir BUILD_DIR --output-on-failure -j1。
