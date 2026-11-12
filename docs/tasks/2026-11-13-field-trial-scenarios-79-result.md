# 阶段79：正式实验在校准前因输入缺失停止

结果：SETUP FAIL / 全部正式验收NOT_RUN / 上机NOT_READY。配置和实验聊天已停止写入并交还root，双端回滚清理通过。该失败未证明产品传输故障。

两端固定交付393f409，运行时dbb2d6b；各完整回归825通过/3原生RDMA环境跳过/0失败、四二进制SHA匹配。临时RX4096窗口在校准监听建立前，calibration79.py读取缺失的ring-control/receiver-full.plan退出，controller断言失败自动回滚。0 payload、无fixture/proxy/GridFTP/CP传输；准入/A/B/反向/安全/C/故障均NOT_RUN，未在同轮修补重跑。

serial-return.json、双端postflight和11项审计确认原2121服务/关键网络配置保持、RX/TX1024/1024，owned监听/挂载/进程为空。124文件哈希通过；Windows归档由root回收，源证据冻结在/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-scenarios-79。压缩证据1327167字节，SHA256 a3fd0102e93543fdd33ce0c05bdc7fa6b64e612bab02c3d92077feae9e46a12d。

root后续静态核对还发现preflight79.py残留preflight77.py引用。下一阶段先在无副作用条件下修复必需plan/本地脚本依赖并冻结输入hash，入口必须在ring/服务/fixture动作前拒绝缺失或变更输入。受影响仓库文件：tools/perf/verify_experiment_inputs.py及本结果/80输入合同说明/当前导航；不修改传输运行时、不重开参数扫描。验收基于原79必拒绝、完整80通过、缺plan副本必拒绝、双端同输入合同核验和时钟/guard合同。未通过新正式组前不得使用历史31Gbps替代新结果。
