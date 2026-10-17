# V3-WORK-UNIFY-03 验收结果（2026-10-17）

状态：done（共同 lease 执行入口、受控恢复与 range 发布修复）；实际客户端自动重连仍待 04。

## 实际解决的问题

02 已使 persistent 上传/下载能够与 global scheduler 共用 queue/dispatch window，但 sender/receiver 分别推进 lease，且断线恢复需调用者手工串联多个操作。03 将已有 queue 路径接入 WorkExecutor，集中处理工作所属 worker、link、epoch、IO drain、result 与失败回收，避免合法但属于其他 worker 的 handle 被误用。

recover 要求断线后所有 active lease 的完整权威对账，先校验后修改。已由接收端确认提交的工作释放而不重发，Missing 才增加 attempt 并重新入队；重试次数有限。调用方必须停止/join 旧 transport，并提供真实接收端证据；API 不自行查询远端或建立连接。

追加回归复现并修复一个可见错误：两 range 中第一个已完成、第二个中断，恢复只补第二个时，原逻辑仍等待本会话收到原始全部 range，导致双方报告成功而目标文件未发布。现在完整 verified coverage 且无活动 writer 即可发布，并允许同一会话后续已验证 range 安全跳过；发布后 DATA 继续拒绝。

## 固定输入、实现与备份

- 输入：739a124c744ac2a4e245670108515ab701063015。
- 主实现：ccd7eda71df8a20b358cfba94c9a2cf03ec01d7b。
- 最终代码及 range 修复：05d2ef68a651ba190a615fd49f2cbd6955a151bb。
- 分支：codex/V3-WORK-UNIFY-03；remote 已核实为 git@github.com:Cantoneseman/CPNetFlux.git。
- 两次代码提交均已 push，分别回读远端同 SHA；最终文档提交后再次 push/回读，结果保存在外部 backup-verification.json。
- 深圳独立 worktree：/root/projects/CPNetFlux-worktrees/v3-work-unify-03；依本轮用户明确的 worktree 规则执行，单写入者。
- 证据根：/tmp/cpnetflux-runs/V3-WORK-UNIFY-03-20261017；源码归档、二进制和日志 SHA-256 见 evidence-index.json。大 payload 未进入 Git。

## 验收及失败记录

- CMake Release，CPNETFLUX_BUILD_TESTS/TLS/IO_URING 全部 ON；最终固定代码 full build exit=0。
- 全量 CTest：354/354 passed，0 failed，0 skipped，73.51 秒。包含 8 轮 cpnetflux_v3_stability_matrix，以及现有 TLS、global scheduler、目录与文件 smoke。
- 相关定向回归：27/27 passed。覆盖外来/旧 epoch/重复 lease、未 drain IO、完整唯一 ledger、明确提交不重传、预算/取消/epoch 耗尽、scheduler ready/inflight/retry 事件。
- socketpair 上传/下载 × whole/range 断线后真实关闭/join，再创建新 socket，4-byte 已验证前缀被跳过、只传缺失 4 bytes，最终内容一致。
- FILE_RESULT 未读但接收端 manifest 已提交时，明确 Committed 对账不重派；该测试未实现远端自动查询。
- 两 range 缺失项恢复发布，以及全量 checkpoint 重放后首个 skip 发布、后续 skip 正常结束，均覆盖上传/下载。
- 真实行为失败：behavior-red.log 中外来 owner 被接受、伪造 size 对账被接受，修复后 behavior-green.log 通过。早期新增测试无法编译仅属编译失败，不作为行为红灯证据。
- multirange-red.log 中上传/下载目标文件不存在，两项失败；修复后 recovery-final-focused.log 全绿。探索中的“已发布上传文件在新会话再次 resume”被既有策略拒绝，未据此扩展能力；回归改为未发布但完整 checkpoint 的真实适用场景。
- ccd7eda 的初次 350/350 记录保留为 ctest-full-initial.log；最终验收使用含追加 range 修复的 05d2ef6，共 354 项。
- 临时测试 token 随机生成，仅注入测试子进程，未写入文档/Git。git diff --check 通过。

复现命令：

~~~sh
cmake -S /root/projects/CPNetFlux-worktrees/v3-work-unify-03 \
  -B /tmp/cpnetflux-runs/V3-WORK-UNIFY-03-20261017/build \
  -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON \
  -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build /tmp/cpnetflux-runs/V3-WORK-UNIFY-03-20261017/build -j 2
# 使用进程级随机 CPNETFLUX_TEST_TOKEN；不要把 token 放进命令或日志
ctest --test-dir /tmp/cpnetflux-runs/V3-WORK-UNIFY-03-20261017/build --output-on-failure
~~~

## 边界与下一步

本阶段是共同 lease facade 与受控恢复原语，未合并所有兼容 worker 编排。客户端 transport 失败仍会经 requestAbort/cancelAll 终止；尚未自动重建 control batch、data/TLS 或查询远端 manifest。不能将 socketpair 验收称为客户端自动重连完成。

V3-WORK-UNIFY-04 解决网络短暂中断仍需手动续传的问题：

1. 检查控制 batch/远端事务的释放和重新进入规则，保持 transferId/generation；区分 transport 故障与用户取消、checksum/身份错误。
2. 停止新 claims，关闭并 join 全部旧连接/reader/worker，获得每 lease 的接收端权威对账，调用 recover 后重建 control/data/TLS。
3. 明确总 deadline 与重试预算跨重连累计，缺失数据才补发；取消、不可恢复错误或预算耗尽及时退出。
4. 用有界 loopback 故障注入验证 upload/download、fixed/adaptive、range、TLS、结果丢失、源变化及取消/耗尽；随后完整 CMake/CTest 和 GitHub 备份。

04 尚未实施。无跨机性能实验或生产 readiness 结论；本阶段未使用 GridFTP，proxy 按用户要求仅在需要时刷新。上海未启动新实验，历史 dirty 树与其他用户实验未修改。本轮自查加自动测试，未声称其他角色已执行独立审查。
