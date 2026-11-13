# Stage86：重启TLS余项与受控上机候选闭环

输入4170f3cc939a3d514e54df0e162920e213df6d11，运行时dbb2d6bac86b29914dbea655b29660937267e38f；无src/include或四二进制变化。单写者实验交接已真实完成并回到root。暂停现场保持，恢复后使用新窗口，不延续失效租约。

双端各108项独立离线用例（101既有+7实际fixture phase用例）、46脚本编译、19安全入口、6项目helper及2实际procfs调用者导入通过。phase fixture测试保留旧重复目录RED、独立before/after GREEN、同phase重复拒绝、非法phase及失败清理。冻结input-contract SHA256为8c244551c2522666812fad0e57081a37d92c34ac8cd3245e3b66661a274cb410。

新受控窗口35.266620秒；controller PID/start278549/118840631、matrix278619/118841056，真实terminal session14964，两个退出码0。校准和独立监督生效。一次1 GiB明文准入全门通过，整任务0.290559546秒/29.563422 Gbps；这是单次准入，不替代阶段80五对或产生新的中位数。

真实TLS重启前后各8 MiB，均包含2文件（一个0字节）、3目录（empty、nested、nested/leaf）。完整外部SHA、目录集合、V3终态、明确数据网bind、控制/数据TLS验证通过。源服务PID/start由1995561/119331708变为1996175/119332257，命令和环境相同；两次服务均停止后才记录PASS。应用及内核pacer off、queued/pwrite/65532 B逻辑DATA；不作大文件加密性能结论。

合并验收仅继承阶段80合格A/B/反向、阶段85合格S、完整C234/1807.619876秒及六项individual跨机F。E85整组FAIL与原postTLS NOT_RUN原样保存；新窗口未重跑C/A/B/S/六F，未相加失败时长。30项复合审计全部通过。root另直接重算234条吞吐中位31.077675与计时合计65.200354秒，并复核1488项C/F哈希及TLS源/目的完整SHA/目录/重启事实；不只采信审计绿色。

双端RX/TX恢复1024/1024，host pswpout仍4693038/2292432，无自有进程/临时监听/挂载/payload/凭据残留；原2121 PID/start保留，RSS/IRQ/路由等不变。root重新读取两端实际四SHA与ring，确认当前现场。两份不相关worker-state未跟踪文档保持。

外包delivery-final.tar.gz：159936482 B，SHA256 99f32d8fba10763db857203425a60e06a02e5950498625c8595196cb7804b68a，1993项；内嵌R2包80347253 B，SHA256 b05d81cb64b2ee455ae8d41f314ccfbe7fc4c8da52119335560efd3854c95d03，1873项。Windows D:/Project/CPNetFlux/_codex_field_trial_tls_closeout_86已留存；root独立流式读取两包所有成员，路径安全、常规文件、每项大小/哈希及serial-return SHA全部一致，0不符。root-verification.json单独留存。最终serial-return/Windows回执在封存包外，不回写冻结历史。

当前交付为[受控上机候选](../release/R770_FIELD_TRIAL_CANDIDATE.md)、[操作说明](../release/R770_FIELD_TRIAL_RUNBOOK.md)和[冻结清单](../release/R770_FIELD_TRIAL_FREEZE.json)。工程、限定比较、重复任务稳定性、六故障与最终TLS门均有独立完整证据；最终文档需选定提交/推送/远端SHA回读并同步R2。大证据包不进入GitHub；不是生产/100G/磁盘持久化/8或64GiB/WAN/原生verbs/加密吞吐认证。
