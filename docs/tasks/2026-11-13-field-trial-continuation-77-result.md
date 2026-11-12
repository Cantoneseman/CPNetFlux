# 阶段77：目录实测暴露空目录语义缺口，停止晋级

结论：阶段75五对1GiB正式主组A有效；本阶段dense/tiny共12个任务通过，mixed第1个CP因缺少空目录失败，反向/安全/C稳定性/故障组NOT_RUN，上机NOT_READY，用户完整目标OPEN。原RX4096候选已结束，双端RX/TX1024/1024，不持久化、不继续发载。

## 版本与修正

双端输入b6aee7216bda89c374acaea956a54bcdcc2995cc，运行时仍42718ac26be24d53ec17525a7b1b3bcb1e340931；两端同配置构建成功、四二进制SHA一致。git diff 42718ac..HEAD -- src include tests tools CMakeLists.txt为空；产品相关回归复用阶段68双端156/156，不称为本轮重跑。所有实际互传显式绑定192.168.100.2→192.168.100.1，V3 TCP、普通tmpfs、非持久化，非原生verbs。

76把R1 monotonic直接用于R2 observer，payload前误停。root接回77后改为按node取原始本机deadline，4项时钟行为回归及双端实际空载观察器27/25次采样通过；12项回滚测试通过。首次root启动另因run76B.py/run77B.py文件名拼接失败，尚未生成数据即回滚；原始77目录完整保留，driver-correction.json记录原因。修正后的77-r1先校验入口实际存在再执行。没有替换、补跑或筛选任何payload性能样本。

## 场景结果

每软件每类3次，精确命令、env、资源/接口/HCA、GSI刷新/有效期、source驻留、外部树/内容hash和cleanup见B-rows、observations、watches及r2-evidence。目录接收合法模式为queued+pwrite，保持4连接、256KiB DATA、1MiB chunk、CRC32C/verified_chunks、commit_sync=none和既有应用pacer。双方普通tmpfs；Grid保留GSI认证/明文数据及冻结最强配置。这些是不同认证/校验语义的配置任务比较，不是等安全通用速度比。

| 场景 | CPNetFlux中位完整任务 | GridFTP中位完整任务 | 状态 |
|---|---:|---:|---|
| 2048×64KiB（128MiB） | 0.399103秒 | 0.944606秒 | 各3次PASS |
| 10000×4KiB（40,960,000B） | 3.504492秒 | 2.561071秒 | 各3次PASS，CP更慢 |
| mixed256MiB、514文件、嵌套/zero/empty | 文件内容PASS；目录语义FAIL | 停止门后未运行 | 不发布mixed吞吐 |

通过的12任务全量内容/目录、宽及刷新后网络/HCA错误/drop/OOB/missed、host pswpout/owned swap/资源/cleanup门通过。tiny的CP完成时间比Grid中位约多36.8%，不能用大文件的31.075Gbps或约2.516倍配置比替代小文件结论。不要将未完成的场景当作通过。

mixed唯一CP：268435456B、514文件、completed_files514/failed_files0、零长文件及嵌套文件SHA都正确；外部文件manifest完全一致，但缺少empty目录，无多余目录。客户端result=pass对应当前file-only合同，不等于完整树语义成功；外层正确拒绝。观察到NIC/HCA及swap增量0。失败后立即请求回滚，收尾期间看到ring reverted是停止信号的结果，不作为新网络根因。

## 根因与后续开发边界

TreeManifest只有vector<TreeFileRecord> files；tree_scan.cpp:130遇到directory直接continue；V3 scanPersistentTree在persistent_tree_transfer.cpp:1381调用该扫描。研究基线早已声明不保留空目录，因而属于既有能力缺口，未证明本轮生产回归或数据损坏。源证据及hash在empty-directory-source-evidence.json。

下一项优先补齐V3目录项、能力协商、清单/恢复与安全创建语义，再重新固定代码做正式比较/完整B/C。只补普通文件数据路径不能修复该问题。RDMA、进一步吞吐调参、微批继续延期。详细开发安排见仓库docs/tasks/2026-11-13-v3-directory-semantics-followup.md（PLANNED_NOT_IMPLEMENTED）。

## 回滚、清理与证据

原授权90分钟绝对deadline逐主机保持，未延长；成功/失败/丢lease/owner退出均受独立监督。controller_error=null、heartbeat_errors=[]、rollback_errors=[]；双端RX/TX1024/1024、RSS/字段/channel/coalesce/features/MTU/地址/路由等关键配置和原2121 PID/startticks均保持原值。两端pswpout4693038/2292432。

最终无owned服务/监听/进程/payload/tmpfs挂载/proxy。目标目录外的7份自有tree checkpoint sidecar在最终清理中被保守拒绝，随后逐文件复制到checkpoint-sidecars并核hash后清除，不删除未知文件。原始拒绝保留cleanup-initial-refusal.json。首轮77仅启动未发载的旧临时proxy也已清理，postflight复核无残留。

3份82MB的tiny原始JSON及175MB服务日志以gzip无损压缩，保存原始长度/SHA和压缩长度/SHA；回放解压核对通过，未删除证据内容。未归档凭据、构建产物或payload。只读audit77.py共83项通过，确认失败事实与证据一致，不将B/C升级为PASS。

复核：python3 -B /home/Su/experiments/CPNetFlux/2026-11-13-field-trial-continuation-77-r1/audit77.py；SHA256SUMS供冻结后校验。原始首次启动证据在/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-continuation-77，真实13任务证据在/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-continuation-77-r1。交付由root提交小结果/冻结与路线至GitHub，完整证据另作Windows归档；外部delivery收据记录精确最终SHA及归档hash。
