# V3-WORK-UNIFY-04 验收结果（2026-10-17）

状态：done。有界自动重连已接入实际 persistent dynamic tree 客户端，并完成深圳、上海两台阿里云服务器互传。下一阶段 05 仍为 planned。

## 解决的实际问题

03 的共同 WorkExecutor 和受控恢复原语需要调用者手工关闭连接、对账和重派；真实 CLI 遇到短暂网络中断仍会失败，用户必须重新执行续传。04 在显式启用后自动完成旧连接 shutdown/join、旧服务端 batch 释放确认、重新认证、重建 queue/epoch 和接收端 manifest 恢复握手。已校验的字节跳过，只补缺失数据；已提交文件也能重新核验，避免 FILE_RESULT/控制完成消息丢失导致重复整文件上传。

增加 --reconnect-attempts（默认 0，范围 0..10）及跨重连累计的 --transfer-timeout-ms。自动恢复要求 persistent dynamic tree、checksum 非 none，不接受 max-files；当前兼容 static/per-file 和通用单文件 CLI 尚无此自动恢复入口。启用示例是在适用的 tree 命令中增加 --reconnect-attempts 2 --transfer-timeout-ms 120000。

仅 transport 故障消耗重连预算；认证、TLS 身份、checksum、源元数据变化和磁盘/事务错误仍失败退出。预算耗尽或 deadline 到达有界退出。同步 DNS 解析暂不可中断，两云使用数字 IP；不能宣称已实现硬 DNS 超时。每次恢复重新启动调度器，不承诺保留 adaptive 学习状态。

## 固定输入与代码

- 输入：28bc94e45eb4892886308a750f4f3d98c89791c8。
- 主实现：33fca2554f83ede5ca45da4fe2437907644b8463。
- 双云回调并发与恢复统计修复：453b9c76419f4c5e0b36172ef33e7c361480cade。
- 最终验收代码：ff71014044ce1a788988ff1ae5a3eb911671e3ee。
- 深圳独立 worktree：/root/projects/CPNetFlux-worktrees/v3-work-unify-04；分支 codex/V3-WORK-UNIFY-04。上海仅部署此固定代码的二进制与测试工具；Windows 仅连接、部署和取证。
- GitHub remote：git@github.com:Cantoneseman/CPNetFlux.git。上述三个代码提交均已 push 并分别回读同 SHA；收尾文档提交的最终 push/回读记录保存在证据根 backup-verification.json。
- 决策：docs/DECISIONS/2026-10-17-bounded-client-reconnect.md。已 cancelled queue 永不复活，采用 attempt 边界整体重建并等待远端释放；03 手动 recover 原语保留。

## 实际验收

最终 ff71014 的 Release/TLS/io_uring ON 构建 exit=0。全量 CTest **357/357 passed，0 failed，0 skipped，99.44 秒**，包含 8 轮既有 V3 稳定性矩阵与新增 cpnetflux_reconnect_smoke。后者在真实 socket 上运行 17 场景：正常传输、双向中断、fixed/adaptive 的 TLS 大文件 range、控制完成消息丢失、实际 FILE_RESULT 丢失、重试耗尽、100ms 总 deadline、源变化、已提交目标损坏和 TLS 身份错误。

两台阿里云服务器交换 client/server 角色，通过公网 IP 传输实际 CPNetFlux payload。控制与数据均 TLS required，使用临时 token；透明代理只对本任务连接注入一次 TCP reset。SSH/scp 只用于部署和收集证据。隔离控制端口 22410、数据端口 23380..23395；原有服务未重启。

每项为 2 文件、67,112,064 bytes（64 MiB 大文件 + 3,200 bytes 小文件），大文件超过动态 range 阈值；下表每行各运行正常和中断两项：

| 客户端操作 | 真实 payload 方向 | 策略 | 正常 | 中断自动恢复 | 恢复跳过的已验证 bytes |
|---|---|---|---|---|---:|
| 深圳客户端上传到上海服务端 | 深圳 → 上海 | fixed | pass | pass，1 次重连，epoch 2 | 986,240 |
| 深圳客户端从上海服务端下载 | 上海 → 深圳 | adaptive | pass | pass，1 次重连，epoch 2 | 524,288 |
| 上海客户端上传到深圳服务端 | 上海 → 深圳 | fixed | pass | pass，1 次重连，epoch 2 | 986,240 |
| 上海客户端从深圳服务端下载 | 深圳 → 上海 | adaptive | pass | pass，1 次重连，epoch 2 | 327,680 |

最终 **8/8 pass**。逐文件 SHA-256 全部一致，接收端 manifest 均完成提交；4 个中断用例满足 recovery_payload_bytes + resume_skipped_bytes == logical_bytes，正常用例无需重连。详见同目录 2026-10-17-v3-work-unify-04-cloud-summary.json；两端使用同一组二进制 SHA-256。

这证明网络中断后实际程序能够自动恢复、保留接收端已验证进度并最终生成正确文件。尚未 checkpoint 的 TLS/kernel 缓冲数据可能补发，累计 payload 如实保留；不承诺零额外网络字节，也不从这些正确性用例推导性能提升、十链路能力或生产稳定性。

## 实测发现并修复的问题

1. 首轮双云上海下载中断出现 duplicate file ID：一个 range callback 在 transport 失败后清理身份，其他 callback 仍继续修改同一身份。现在首个 transport 失败立即发布，兄弟 callback 停止修改并退出；新 epoch 等旧 batch 完全释放后再进入。
2. 旧 skippedBytes=expected-payloadSent 将失败后尚未发送的字节误算成恢复跳过。现以恢复握手的缺失清单计算，新增恢复 payload 统计并检查恒等式。
3. 首轮 CTest 的固定“最多额外发送 4 MiB”断言不符合 TLS/kernel 缓冲行为；替换为可验证的缺失/已验证字节对账，累计 bytes_transferred 继续保留真实值。
4. 修复不能改变普通逐文件失败后继续其他文件的兼容语义。第二轮全量回归发现该问题后，将立即中止 sibling callback 限定为 TransportError；最终 ff71014 全量回归及双云八项全部重跑通过。

初始、第二轮失败日志和 validation 分开保存，未覆盖成成功。behavior-red.log 记录旧客户端在真实连接中断后失败，checkpoint-not-created-red.log 记录 FILE_BEGIN 到 checkpoint 创建之间中断的边界；单纯参数不支持或编译失败不作为行为红灯。

## 复现入口与证据

~~~sh
cmake -S /root/projects/CPNetFlux-worktrees/v3-work-unify-04 \
  -B /tmp/cpnetflux-runs/V3-WORK-UNIFY-04-20261017/build \
  -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON \
  -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build /tmp/cpnetflux-runs/V3-WORK-UNIFY-04-20261017/build -j 2
# 认证测试使用进程级随机 CPNETFLUX_TEST_TOKEN；不写入命令历史或日志
ctest --test-dir /tmp/cpnetflux-runs/V3-WORK-UNIFY-04-20261017/build --output-on-failure
~~~

双云入口为 tools/test/run_cpnetflux_cloud_reconnect.py 的 serve/client/inspect，serve 指定本机私网 listen-host，client 指定另一台公网 peer；分别选择 upload/fixed、download/adaptive，fault=0/1，再交换两端角色。每项必须新 root；deploy 提供同 SHA 二进制、证书及权限 0600 的临时 key/token。serve ready 后执行 client，结束后创建该项 root/stop，等待 serve 终态并读取双方 result；不能仅依据启动成功算验收。

两端证据根均为 /tmp/cpnetflux-runs/V3-WORK-UNIFY-04-20261017。深圳保存：

- validation.json、build.log、ctest-full.log：最终固定代码构建与全量测试；initial-/v2- 文件保留失败历史。
- cloud-final/：本机各项 client/server 原始日志、summary、metrics、fault 与进程回收记录；cloud-final-orchestration.json 记录八项终态。
- cloud-final-summary.json、cloud-final-host-report.json、cloud-final-evidence.tar；上海原始证据在上海同路径，另汇总为深圳 shanghai-cloud-final-evidence.tar。
- evidence-index.json：输入/最终代码、源码归档、编译器、环境、日志及归档 SHA-256；同源二进制哈希也在 Git 小型云测摘要。
- cleanup.json、shanghai-cleanup.json：只删除本任务可再生 payload/临时凭据；诊断归档保留，未将大 payload 放入 Git。
- backup-verification.json：收尾提交与 GitHub 分支 SHA 回读。

固定代码源码归档 SHA-256：b47588849489f60aa95fdffe376087a25fdd1b6a5df8c74a60da793c8e1110de。
深圳云测证据归档 SHA-256：e3c771371f083811da15d2ef387ae2587a0e06f6dcdc57570c4fc4550bc15563。
上海云测证据归档 SHA-256：6259cb338477a1168d8f3e6f25704c8b6e4fda40ae7e20fb193fdf4f201aca0d。

已核验两端本任务进程全部退出、测试端口全部释放。多轮本地/双云数据累计超过初始单轮 2 GiB 估计，清理深圳约 5.12 GiB、上海约 1.88 GiB；清理后分别约 67.96/81.59 GiB 可用，执行前后资源采样均高于 10 GiB 保留线。临时凭据/部署包已移除，原始诊断保留；重跑须重新生成测试凭据。历史 dirty 树和其他用户实验未修改。验收为本轮自查与自动测试，未声称其他聊天已执行独立审查。

## 尚待解决与下一步

父项 02 尚未完成“全部兼容编排共用一个生命周期”：static/per-file 仍有兼容路径，取消/超时/结果处理不能只依赖 persistent dynamic 的成功。下一阶段 V3-WORK-UNIFY-05 先盘点所有实际分支，将兼容编排收口到共同 WorkExecutor/queue owner，并保留既有顺序、逐文件错误继续处理及 requested/actual 指标。补充主动取消、超时与旧回调退出的行为门禁，之后再扩展 LinkSet/多物理链路。

05 计划见 2026-10-17-v3-work-unify-05.md；本轮未把该计划标为已实现。
