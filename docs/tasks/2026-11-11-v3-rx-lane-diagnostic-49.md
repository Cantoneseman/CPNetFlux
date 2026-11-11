# V3 有界TCP lane诊断与停止门49

日期：2026-11-11
输入提交：4080122d0454b17c5d1ac0b50745e49d41dfb0e4
状态：DONE（工程完成；首个单路样本RX门中止；无性能结果）
结果：2026-11-11-v3-rx-lane-diagnostic-49-result.md

## 范围、依据与假设

当前总指挥接回唯一写入权；“配置和实验”最近轮次已结束，临时服务/payload清理。
新证据见data-encryption-ab-01-result：GridFTP明文1GiB也记录R2 missed/OOB +104，
说明现象不局限于CPNetFlux mmap；不证明所有历史事件同因。原Task48的mmap固定
四路约束不适合只改变lane数，因此改用同一pwrite/queued路径对比1/4路。

修复现有run_receive_network.py：移除固定四路mmap和传输期间全量观察器，
最多1/4路各一个1GiB完整V3 TCP任务。分开记录进程退出、0.5秒延迟刷新、
本地SHA后的宽计数窗口，并验证清理；任一网络/资源门失败停止后续任务并保留已完成样本。
不复跑Task46/profile矩阵，不将诊断速率当正式成绩。

## 非目标

不修改C++数据路径、TLS/RDMA、协议、默认配置、网卡/内核/IRQ/ring/swap。
不增加并发上限/缓冲预算，不进行正式8GiB或GridFTP重测，不派代理/建worktree。
已有34.159Gbps M1成绩保持历史条件，不宣称M2或根因解决。

## 验收

- 先用故障注入回归证明旧driver在OOB出现后继续下一样本，随后确保停止。
- 1与4路命令除连接/文件并发及输出路径外相同；256KiB DATA、请求1MiB接收buffer、
  1MiB chunk、CRC32C/verified_chunks、checkpoint16、pwrite/queued、profiler关闭。
- 网络窗口不包含外部SHA；延迟刷新单独记录，只能归因接口范围。
- 两端同提交、配置、构建、二进制SHA核对；先提交推送再同步R2后互传。
- 内存可用至少12GiB、owned swap及host pswpout为零，source全页驻留，
  每臂全SHA/manifest/清理，首个异常停止。无重复补跑。
- Python相关脚本测试与相关CTest通过，结果/失败证据/清理/提交回读归档。

## 受影响文件

- tools/perf/run_receive_network.py
- tools/perf/run_small_file_baseline.py
- tools/test/run_receive_network_test.py
- tools/test/run_small_file_baseline_test.py
- docs/tasks/2026-11-11-v3-rx-lane-diagnostic-49.md
- docs/tasks/2026-11-11-v3-rx-lane-diagnostic-49-result.md
- docs/ROADMAP.md
- docs/README.md

证据目录：/home/Su/experiments/CPNetFlux/2026-11-11-v3-rx-lane-diagnostic-49
