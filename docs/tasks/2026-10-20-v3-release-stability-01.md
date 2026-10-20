# V3-RELEASE-STABILITY-01

Date: 2026-10-20 (Asia/Shanghai)
Status: slice A complete; parent release-stability task remains in_progress
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
