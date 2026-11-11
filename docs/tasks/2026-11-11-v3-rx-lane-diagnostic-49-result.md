# V3 有界TCP lane诊断与停止门49结果

日期：2026-11-11
输入提交：4080122d0454b17c5d1ac0b50745e49d41dfb0e4
实现提交：341c340ed8e02fcc84c65f2e2b4fb6d8686b1ea0
实机输入提交：087769b3a9ea9f586752825396cda9c325da2942
状态：工程完成；诊断在首个1-lane样本因RX门中止；无性能验收结果。

## 本轮交付

复用现有网络driver与client_sample，支持同一pwrite/queued路径的1/4 TCP lanes，
每种最多一次1GiB。移除旧driver的固定四路mmap、传输期间全量观察器及可重复矩阵入口，
关闭详细receiver/range profiler；保留10ms资源保护采样及低频ss快照，故非零观测开销。
将网络采集分为客户端进程边界、0.5秒延迟刷新尾段、两者累计，并在外部SHA前完成这些快照。
外部SHA后的宽窗口另行校验missed；它不是清理后的计数或流级归因。
任一OOB/missed/resource异常都阻止后续任务，保留receipt、failure、结果及清理证据。
默认传输配置与C++运行时均未改变。

故障注入先证明旧driver出现OOB后仍继续，随后通过首样本/刷新尾段/四路异常和双臂正常测试。
实机前发现CLI入口缺少传给远端生成器的size_gib，增加不带该字段的入口回归，
观察AttributeError失败后补齐固定值1，提交087769b；修正前未开始互传。

## 固定配置与版本

两端同087769b、RelWithDebInfo，CMake选项、三个运行二进制SHA完全一致：
- cpnetflux-gridftp-server：125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5
- cpnetflux-tree-download-client：20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c
- cpnetflux-tree-upload-client：b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0

R770-2→R770-1，驻留tmpfs源/全新tmpfs目标，1GiB，
显式绑定192.168.100.1/.2及eno17095np0，V3/TCP、TLS off，
256KiB逻辑DATA、请求1MiB接收buffer（实际2MiB/socket），
1MiB chunk、CRC32C/verified_chunks、checkpoint16、commit_sync=none，
sender/receiver queued、pwrite，pending-window2、depth2、batch1、预算64MiB。
该配置与M1的inline mmap接收不同，不直接比较成绩。

## 唯一样本

| 项目 | 结果 |
|---|---|
| 尝试/跳过 | 1 lane完成；4 lanes因停止门未运行 |
| 客户端进程/driver退出码 | 0 / 1（网络门主动拒收） |
| 进程完整任务墙钟 | 0.352790585秒；不包括外部SHA |
| 诊断速率 | 24.3485Gbps；网络门失败，不是有效性能成绩 |
| 空载0.5秒 | OOB/missed均0 |
| 客户端边界窗口 | R1 rx_out_of_buffer +171、rx_missed_errors +171 |
| 刷新尾段0.5秒 | 两项增量均0 |
| 累计/本地SHA后宽窗口 | OOB/missed +171；宽窗口missed +171 |
| R2发送端服务宽窗口 | missed增量0 |
| 完整性 | 全SHA树一致，1文件/1GiB、1,024 verified chunks、member/tree manifest均完成 |
| 源驻留 | 262,144/262,144页全驻留 |
| 最低MemAvailable | R1接收29,636,501,504B（27.60GiB）；R2服务29,033,574,400B（27.04GiB） |
| 资源门 | 两端owned swap峰值0，源准备/服务/客户端及整轮host pswpout增量0 |
| 清理 | 两端自有payload root消失，/dev/shm均0B使用，临时服务退出 |
| 外部服务 | 2121原PID/starttime保持R1 126766/1191040、R2 164834/1679976 |

SHA树：8a7e98849c21820d2b5aa8b12b4baec7e99a59b0fd48907ed009298b3535a30c。
client_sample receipt中的result=pass表示传输及内容校验通过，不代表本轮诊断/性能门通过；
failure.json与diagnostic-audit.json保存最终拒收结论。

## 能够与不能够推出的结论

单路、pwrite、无详细profiler条件也能伴随接口OOB，降到一路没有避免本次现象；
不能继续假定只在四路或实验mmap出现。本轮未运行四路，没有lane排名或配对收益。
Task48确认OOB是shared-device Q counter聚合，以下逐队列数字仍不能给OOB指定RQ或流。

客户端边界窗口主要RX packet增量为rx8=742,644、rx22=12；
rx8 page-pool fast +368,029、refill +3,299，
empty/slow/slow_high_order、buff_alloc_err、wqe_err均0。
softnet dropped/time_squeeze均0；CPU8在该短窗口busy约79.4%、softirq约52.9%。
发送端ss观察到该data socket bytes_retrans=247,608、retrans=171、
rwnd_limited约68.8%。这些与RX压力/背压相容，但单次瞬时计数和接口聚合不能证明
包突发、page分配失败或CPU8饱和就是原因；计数相同也不能证明逐包对应关系。
外部SHA在窄窗口之后，故本轮+171并非因计数窗口包含外部SHA产生；
异步驱动刷新与接口上其他流量边界仍适用，不宣称流级因果。

## 验收与证据

两端CMake增量构建成功；相关CTest各7/7通过：
cpnetflux_receive_network_test（内部11测试）、
cpnetflux_small_file_baseline_driver_test（内部13测试）、
cpnetflux_large_data_ab_test（内部14测试），以及四个大帧/恢复smoke。
R1 8.93秒，R2 9.26秒；Python警告作错误语法检查与git diff --check通过。

命令：
```bash
ctest --test-dir build -R 'cpnetflux_(receive_network_test|small_file_baseline_driver_test|large_data.*)$' --output-on-failure
python3 tools/perf/run_receive_network.py --evidence /home/Su/experiments/CPNetFlux/2026-11-11-v3-rx-lane-diagnostic-49 --payload-root /dev/shm/cpnetflux-s0-rx-lane-49 --commit 087769b3a9ea9f586752825396cda9c325da2942
```

证据根：/home/Su/experiments/CPNetFlux/2026-11-11-v3-rx-lane-diagnostic-49
- matching-builds.json、inventory-r1.json、preflight/postflight两端证据
- results.json、samples/network-lanes-1.receipt.json、summary.json.gz、client.log.gz
- failure.json、server.json、cleanup.json、diagnostic-audit.json
- idle-analysis.json、entry-red-test.log、red-test.log、r1-ctest-entry.log
- peer-evidence-r2/保留远端seed、source及服务/资源证据，不复制payload进Git

## 下一交付与总体进度

本轮停止，不补跑四路、不重复profile或GridFTP矩阵、不调整NIC/IRQ/ring/RSS。
M1仍为历史固定条件下34.159Gbps；M2 40–50Gbps、后续70–85/100Gbps尚未验收。
三阶段工程/正确性资产和大帧/接收buffer显式能力保留，网络稳定性仍是M2阻塞项。

下一候选收敛到发送端有界节奏/背压控制：先核对现有发送入口和任务局部pacing能否实际
生效，再选择一个显式、可撤回且总内存不增长的实现；不先做多参数扫描。
假设是平滑突发可能减少接收服务压力，不把现有证据当作已证实根因。
若需系统配置变更，则不在当前合同内自动执行。候选通过机制/正确性后才安排一次
有停止门的1GiB可行性验证，复现OOB即停；即使无OOB也不自动宣布稳定或吞吐达标。
若仅靠降速减少事件，必须报告吞吐代价，不包装成性能提升。
