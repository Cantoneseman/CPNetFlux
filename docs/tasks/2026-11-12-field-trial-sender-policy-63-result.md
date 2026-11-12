# 阶段63结果：既有应用 pacing 的私有后端准入验证通过

2026-11-12。状态：工程入口完成 / 单次假设通过 / 正式比较与稳定性未完成。
[范围与验收](2026-11-12-field-trial-sender-policy-63.md)、[冻结清单](2026-11-12-field-trial-sender-policy-63-freeze.json)。
实现与受测固定提交 db62ec6fe7b9c65734c1d0e282ec76306ffae448，GitHub回读及R2同步已确认。

## 实际推进

“配置和实验”完成隔离GSI trust修复、双向8MiB认证smoke后结束turn并串行交回。
当前聊天核验PID/start_ticks后接手。R1到R2管理SSH认证不可用，已实现仅绑定loopback、
经Windows现有SSH别名转发的白名单控制通道；数据仍沿192.168.100.1/.2直传。
没有新建SSH密钥、修改authorized_keys或系统网络。协议/截断/拒绝测试双平台通过，
实际固定版本receipt经通道成功；一次wrapper退出错误先被拒绝，清理后修正，未计为传输。

最新内核ceiling强配置的1GiB完整任务内容、资源、大页和清理通过，但OOB/missed各+1344，
按网络硬门拒绝。原始完整时间0.254724173秒、33.722495Gbps不是合格性能成绩。
同字节源的GridFTP四流1GiB诊断完成SHA、零网络/host swap增量，原始完整时间
0.765392905秒、11.222909Gbps；该诊断缺少完整peer峰值观测和正式配对，不是正式对比成绩。
不得将两个原始数值相除作为速度优势。树摘要与原始文件SHA混用曾在传输前触发保护，
已修正摘要类型并将准备验证纳入finally，保留原失败。

本次只补已实现应用pacer的实验参数入口：off或1500000000 B/s/lane，默认off；
仅admission/V3可显式开启，禁止和kernel pacing并用，正式8GiB profiles保持原合同。
Python、native helper和V3驱动逐级传递并复用原run_sender_pacing.validate_receipts验证。
未修改src/include或三传输程序；没有新pacer算法、速率扫描、网卡/IRQ/MTU/cache/swap设置。

## 开发验收

4项行为测试先RED再GREEN，含错误参数、互斥/非V3/正式profile拒绝、旧调用默认off，
以及真实native parser在无特权边界停止，未触发挂载。
两端同配置构建成功，相关CTest各7/7、无skip，含native完整生命周期、原M2/私有driver、
kernel pacing、sender pacing和fixture清理。不是在新提交重跑全仓或先前278项。
三传输程序SHA保持阶段60；helper因参数入口变化更新为
dba043846ccf0206d1ed00c3436221ab92920174e412c5f4e42e116c97f369fb，双端四SHA一致。

## 一次完整1GiB假设验证

固定源：官方source_block(True)的8MiB伪随机块重复128次，实际写满1GiB并全部驻留。
对照同字节内容；压缩off。记录中的树摘要c4f0cf04...与原始文件SHA c5c96262...是不同摘要。
配置：四路TCP、256KiB逻辑DATA、1MiB chunk、CRC32C/verified_chunks、checkpoint16；
queued发送、inline mmap接收、每路64MiB映射、64MiB payload预算、接收buffer请求1MiB；
native私有tmpfs huge=within_size，应用pacer每lane1.5e9 B/s，kernel ceiling/off、
exact-read/off、TLS/off、commit_sync=none。默认不晋级。

| 项目 | 实际结果 |
|---|---|
| 必要namespace/setup | 0.009556636秒 |
| 目标目录准备 | 0.000709636秒 |
| client启动至成功退出 | 0.204890337秒 |
| 完整目标任务时间 | **0.215156609秒** |
| 完整目标任务吞吐 | **39.924103Gbps** |
| chunk / DATA帧 | 1024 verified chunks / 4096 DATA帧 |
| pacing | 四lane均有timerfd和完整字节/spacing回执，spacing violations=0 |
| 接收实际大页 | 16次采样，目标ShmemPmdMapped峰264241152B；不是全文件覆盖 |
| 网络 / 资源 / 内容 / 清理 | 全部通过；OOB/missed与host swap增量0，自有swap0 |
| 旧40Gbps正式准入 | formal_admission=false，M2未通过 |

外部SHA/源准备/观测/清理单列；pacer累计wait_ns跨lane相加不能当作墙钟时间。
两次任务的RX队列分布不同，一次零事件不能证明OOB根因已解决或稳定消除。
本次39.924是单次可行性，不是五对中位、CV、跨提交收益、8GiB M2或稳定性成绩。

## 清理与当前边界

两端本轮普通tmpfs、私有目标、源与partial均已清理，/dev/shm使用0。
四个隔离GridFTP服务均按PID/start_ticks停止；新控制relay已结束并删除私有token。
原2121 PID/start_ticks保持。阶段61的四份8MiB认证payload在核对原SHA/身份后删除，
认证日志、manifest、凭据和历史证据保留，未清理旧项目/历史64GiB数据。

8GiB仍因R2物理free不足9.25GiB记NOT_RUN；没有放宽资源或网络门。
新正式比较配对0；>=30分钟且>=100任务的双机稳定性未开始；上机ready=false。
当前只是完成使对比能够继续的有界入口，不宣告整个用户目标完成。

## 继续执行

1. 冻结此候选用于比较；GridFTP先做有边界的有效配置筛选，双方源字节匹配、
   新目标和完整计时，补齐两端峰值/终态/路由/HCA/清理证据，再做五对AB/BA。
2. 覆盖可准入1GiB、dense/tiny/mixed、双向和单列TLS/GSI private组；未准入规模保留NOT_RUN。
   任一硬门失败停止并分析新证据，不重跑同配置挑零事件样本，不以保守替代冒充强配置稳定。
3. 比较后在同一选定配置做>=1800秒且>=100完整任务，并补常驻服务FD/线程/RSS趋势及
   真双机取消/断连/恢复/损坏拒绝/重启/TLS；不能仅重启服务掩盖泄漏。
4. 全部门成立后才能完成上机启动、canary、资源限额、取消恢复和回退说明并评估上机候选。

证据根 /home/Su/experiments/CPNetFlux/2026-11-12-field-trial-execution-62。
离线复核：python3 此根/verify_phase63.py。最终提交/备份回执留此根policy63-delivery.json，
避免受版本管理文件自引用最终SHA。私有凭据/relay token不进入冻结或Git。
