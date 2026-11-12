# 阶段74：临时RX ring4096准入及回滚

输入提交129d540883502c673266e07d5ed90b2e58df3387；应用运行时仍为42718ac26be24d53ec17525a7b1b3bcb1e340931，默认和四个二进制未改变。用户已明确授权双端eno17095np0 RX1024→4096，在原90分钟窗口内验收、结束或失败恢复1024，不持久化。

双端独立lease/进程身份/截止时间监督器及finally回滚已实际运行。9项行为测试覆盖owner退出、lease丢失、截止、主动恢复、部分应用失败、读回异常及回滚失败不可误报成功。只改变RX；TX1024、RSS key/table/字段、channel、coalesce、features、路由、MTU、地址及原2121服务身份均核对不变。4096字节双向校准实际验证4个接收CPU/NAPI；未加载BPF/perf/strace。

唯一1GiB准入全门通过。必要live RSS/plan预检0.005952754秒、native目标setup/目录创建/client窗口合计0.257369576秒，严格完整任务0.263322330秒，32.621367819 Gbps。脚本旧字段33.375874169 Gbps缺少上述预检，不作为严格完整任务成绩。该单次准入不是收益对照、正式比较、M2或稳定性结论；scope中沿用的instrumented字样不代表本次有BPF，实际无重型探针。

两端source准备/重配置/实际任务的资源观察均无违规，owned swap峰值0、host pswpout增量0；内容、驻留页、CRC/verified_chunks、显式应用pacer、native setup和清理门通过。额外RX headroom静态估算约192MiB/端，不是实际内存上限，采样不保证瞬态峰值。

准入后阶段75在同一候选设置完成五对主组，随后B因脚本非法off值在payload前退出，立即请求回滚。74控制器实际FINISHED，controller_error=null、heartbeat_errors=[]、rollback_errors=[]；双端读回RX/TX=1024/1024，关键网络配置恢复/保持原值。所有阶段74owned payload、挂载、监听及数据进程清空；原2121 PID/startticks为126766/1191040、164834/1679976。

原始证据/home/Su/experiments/CPNetFlux/2026-11-12-rx-ring-headroom-74。只读审计audit74.py共28/28通过，含原始对端48个文件。实际两端postflight仍处于阶段75输入837616dfe9715c6fabba5cdc361b05a5fa5213ba，四二进制一致。产品回归沿用阶段68同运行时的双端156/156，不冒称本轮重跑。

下一步只修正B实验配置并验证场景合法性；已通过的A结果保留。用户完整目标仍OPEN，不以一次准入证明ring是唯一根因或100G能力。继续同4096候选必须沿用最初绝对截止，不延长90分钟，不扫描容量，失败或完成立即恢复。
