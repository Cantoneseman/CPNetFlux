# DIR-ASYNC-CONTROL-03 交付记录

日期：2026-10-09；路线 R2026-10-09.1 / v2。输入提交 d362ea2360bf0cbfe3626a9c93734368a74ec0b6。
隔离分支 codex/DIR-ASYNC-CONTROL-03，源码 /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/src。
状态：代码门通过；短脚本待最终门禁；跨域性能未验收。没有启动深圳—上海实验，没有扩大矩阵。

## 改动与边界
- depth=N 现在使用 N+1 条独立控制连接，1 个当前文件加至多 N 个 pending；支持 0/1/2/4。file_parallelism 的数据并发上限不变。
- slot 候选独占，文件索引/路径/方向/slotIndex/generation 全部校验。读取终态回复后才复用；失败先 shutdown 本任务控制，再 join 和销毁。
- 增加 control_prepare、transfer_complete_wait 的 count/累计秒数、slot 数和 pending 水位。秒数使用单调时钟；count=0 为 null，关闭计时不输出。边界与并发累计限制见任务契约。
- depth0 保持兼容；depth>0 仍拒绝 resume/max-files。本次没有新增流水线 resume 支持，也没有更改数据帧或 manifest 格式。
- 真实控制回复超时为 30 秒；DNS/connect/TLS 握手沿用原行为，不声称整条连接生命周期都已有同一 deadline。
- 00 负责 C++/unit/整合；既有 04 聊天负责独立 loopback/fault；既有 02 聊天负责短实验入口。没有创建新角色或临时子代理。

## 已执行验证
环境：深圳 Ubuntu 内核 5.15.0-181-generic；g++ 11.4.0、CMake 3.22.1、Python 3.10.12。Release、TLS ON、测试 ON、io_uring OFF。
- CMake/Ninja 构建成功。全部单元测试 198 项中 197 pass、1 io_uring skip；skip 不算通过。
- 9 项目录 CTest 全部通过：upload/download/resume/parallel/control_reuse/pipeline_sidecar/changed_file/edge_cases/manifest_corrupt，最终组合运行 41.15 秒。
- sidecar 覆盖 depth0/1/2/4 × fp1/8 × 上传/下载，独立 SHA-256、文件集合、空文件、普通用户 sidecar 命名、控制数、pending 界限和阶段计数。
- 新独立 fault 脚本直接调用真实客户端及真实服务端。双向观测到第二文件 start 早于第一文件 complete；上传 pending 450 拒绝在 0.094 秒退出、断线在 0.095 秒退出，二者具有目标诊断；hold 第二条 150 回复实际在 30.075 秒返回控制超时，非外层 45 秒 watchdog。代理控制线程在强制 stop 前收敛，各模式观测到两个客户端 EOF。
- 旧构建在新 sidecar 的控制连接数断言处失败；新构建通过。旧断言“depth1 下载最多2条连接”已纠正为2条池连接+1条扫描连接。
- 本轮没有全量运行所有 CTest，也没有测试 WAN/TLS 故障注入。fault 注入覆盖上传 depth1；双向深度矩阵与共享取消代码的覆盖不能等同所有故障组合全测。

实际命令（在上述 src/root 的隔离路径）：
```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF
cmake --build build --parallel 2
./build/cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R '^cpnetflux_tree_(upload|download|resume|parallel|control_reuse|pipeline_sidecar|changed_file|edge_cases|manifest_corrupt)_smoke$'
python3 src/tools/test/run_gridftp_tree_async_control_fault_smoke.py --build-dir /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/build
```

## 证据
外部目录 /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/evidence：
build-final.log、unit-all.log、ctest-final.log、current-binaries.sha256、environment.txt。
qa-recovered-output.json 是从既有04会话实际命令输出回收的 JSON，含独立 sidecar/fault 的退出码和诊断。该 smoke 的临时 event-log 路径在结束后删除，不把那些路径说成仍可读取的归档。

当前测试二进制 SHA-256：
- upload: 8245953982a97a68292e340498d001a7582f3ef4aabb1a8d3a66a370f8192f4f
- download: dd86f7ad834b4691284f4c379d1ac8010cbeca2a2dc270f4d84f9e75db000bfa
- server: 76f30ddbaded4ec3f90f14bc602a4ee63e5489eb1f5be712cce9325d4deb727b

## 下一步
短实验固定 128×1MiB、fp1/n1、双向、各3次：CPNetFlux depth0/1/2/4 加真实 GridFTP，共30项。只由用户手动启动；运行前检查两端预算、构建提交及对端二进制 hash。新脚本取代旧宽矩阵入口。
报告区分 runner wall/128 摊销耗时、客户端阶段累计秒数和 GridFTP 吞吐比。数据齐备不等于性能通过。未观察到每文件摊销耗时下降前，不扩大矩阵；90% GridFTP 是后续目标，当前不宣称达到。
提交后的完整 SHA、源码 archive SHA-256 和 GitHub 远端 SHA 回读保存到外部 evidence/release.json，并在总指挥最终答复给出；仅本地 commit 不能称为已推送。
