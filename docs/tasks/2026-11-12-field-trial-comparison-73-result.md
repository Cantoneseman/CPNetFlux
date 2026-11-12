# 阶段73：无探针正式比较因网络门中止

固定输入71b7e94964f2c3896b04970a8144759004d8af06；运行时仍42718ac26be24d53ec17525a7b1b3bcb1e340931。
既有“配置和实验”聊天在root停止根目录写入后串行执行，结束turn01a1d043-56e1-78f3-8d19-d58a7c133866，
serial-return确认清理及释放；root已读取原始产物、复跑只读审计并核验132项原始证据hash。
无产品源码、默认值、Git index或系统网络改动；最后回归仍按阶段68两端各156项的同源码/四hash复用。

## 交付结论

**首个CP 1GiB实际样本触发网络硬门：R1 rx_out_of_buffer与rx_missed_errors各+327。**
文件内容、1024 verified chunks、外部SHA及资源门通过；四lane实际RX队列9/27/7/28，bind均首试成功。
停止其余负载，GridFTP服务和GSI proxy已准备且有效43200秒，但尚未传GridFTP payload。
正式完整配对0/5、有效性能样本0，NO_GO；目录/反向/安全B组和稳定性C组NOT_RUN，上机NOT_READY。
没有正式比值、CV、稳定吞吐或100G结论；历史工程C闭环不能替代整个上机验收。
阶段72有探针的零错误不被改写为修复，本次失败也不能独自证明探针是唯一变量或某个flow造成OOB。

## 内容、性能口径与资源

.2 resident tmpfs源→.1全新native私有tmpfs，V3 TCP而非verbs，TLS/压缩off，CRC32C/verified_chunks，
commit_sync=none；沿用固定4lane、256KiB DATA、1MiB chunk、queued/inline+mmap、应用pacer1.5e9B/s/lane。
实际源/目标外部文件SHA：03bf77edb1d2db399f95e426fc57b415c760c750619427383e103d7a2c29cdea。
client窗口0.231679986s/37.07672Gbps；包含RSS/plan预检、native setup、mkdir和client的完整任务
0.250739717s/34.25837Gbps。**均为失败诊断数字，不计正式成绩**；源生成、外部SHA、观测和清理分列。
没有加载BPF/perf/strace。R2/HCA错误无增量，owned进程/leaf swap及host pswpout增量0；
R1/R2最低MemAvailable为29399015424/28999340032B，owned RSS峰317235200/72925184B。
实际memory.max/high仍max，不称有限cgroup硬上限；8/64GiB未准入且未运行。
宽窗TCP RetransSegs+5、Timeouts+2、SynRetrans+2为主机聚合，未证明属于目标flow；每队列分配/WQE错误为0不能排除接收服务问题。

## 清理、审计与备份

原2121 PID/startticks保持126766/1191040和164834/1679976。
源/目标、私有挂载、49337/2873/2874监听、临时Grid服务及本轮proxy均清理；无owned运行数据进程。
postflight初始把自身观察器命令字符串匹配为owned，按PID/startticks排除后重采验证；未因此kill进程。
9个脚本语法检查、19/19只读一致性审计通过；不是性能PASS。
原证据根/home/Su/experiments/CPNetFlux/2026-11-12-field-trial-comparison-73，132个文件清单冻结。
只读重放：python3 -B 该目录/audit73.py。root双端最终CMake构建、GitHub SHA回读和Windows归档在delivery目录另记。
保留R1两份无关worker-state untracked，无reset/clean，无CPSS/旧证据更改。

## 已定位的wire统计边界

72的TCP唯一双向字节比summary wire_bytes多768B，与8×(64B header+16B应答头+16B单个missing range)吻合。
源码persistent_tree_transfer.cpp:2430写ResumeResponse未累加接收统计；frame.cpp:459编码长度16+16N。
这是源码与观测共同支持的统计遗漏解释，未抓取payload逐帧解码；不会据此推断文件损坏或关闭OOB因果。
本轮保持版本，不修改热路径；后续wire效率分析须注明或先修计数，logical goodput分母不受此计数影响。

## 下一步

停止无新假设的同配置负载和参数扫描。已准备阶段74单变量RX ring临时调整/回滚提案，当前WAITING_USER_AUTHORIZATION，未执行ethtool -G。
两端当前RX=1024、上限8192，32个combined queues；可检验接收headroom是否影响失败，不能预先宣称唯一根因或保证收益。
这是系统网络参数，AGENTS.md第21行明确“不擅自改系统网络”，需用户明确授权后才可实施。
若获准，先有界准入，成功后才恢复新五对/场景/1800秒且100任务稳定性；任何硬门失败停止并回滚。
完整用户目标继续OPEN；该授权边界与已完成的工程备份分开，不冒称交付C全通过。
