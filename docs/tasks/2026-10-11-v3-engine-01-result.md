# V3-ENGINE-01 结果
输入 e88d46c3d43f08190193fa4907815834f84028c6；证据 /tmp/cpnetflux-runs/V3-ENGINE-01。
单文件上传/下载 STOR/RETR 与目录现在共用 PersistentDataSession、FileTransaction、动态元数据队列和 range coordinator。四个原引擎替换为参数适配器，移除约3700行独立状态机。CLI、控制命令、manifest v2 继续兼容；旧单文件 wire 不能互操作，必须客户端/服务端同版本部署。
已接入 checksum/backend（写前 DATA CRC 与 FILE_END 验证）、压缩/恢复原始回退、POSIX/io_uring 接口/缓冲/建议、预分配、manifest策略、final-verify、fsync、原子覆盖、TLS、部分传输故障注入、控制断线与阶段/runtime统计；单文件最多64连接，目录仍最多8通道。
旧 chunk manifest 可读，range resume 按缺失 chunk 覆盖发送；测试 old manifest 缺4/8字节仅发送4字节。损坏 DATA 不得进入 checkpoint。
V1 per-file 编排和 global scheduler/control pipeline 作为同一 V3 内核的策略适配继续存在，summary 标注 actual_transport=v3、per_file 或 persistent_tree 调度，已经不是两套传输状态语义。
验证以 ctest-release.log、targeted-release.log 和 loopback-release-*.log 为最终依据；先定向，再全量，再dense/mixed/large。io_uring OFF时环境项明确 skipped，不宣称真实 io_uring 运行通过。
自检范围：写前身份/偏移/checksum、临时文件/覆盖、commit覆盖、resume错误、断线回收、TLS线程访问、输入变更、控制pipeline兼容。未启动独立审查代理。
下一阶段 V1-RETIRE-01 清理闲置 wire codec 与旧fallback文字，明确旧wire拒绝；然后 EXP-V3-GRIDFTP-01 启动有限对照。
