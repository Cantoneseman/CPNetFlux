# 8 GiB 大文件 M1 验收 41 结果

执行日期：2026-11-11。计划：[M1-41任务合同](2026-11-11-v3-large-file-m1-41.md)。
输入源码提交：`67c4e94a29514449f14da212dfc35534aa911747`；计划提交：`bf1e104a7dd63df6559b80be89c98e19270a3688`。
实现提交：`5f154278fee3a6ec67af6175103a7bbf9ac90ddb`，已推送至CPNetFlux GitHub remote并同步R770-2。

## 结论

M1 **未通过，且没有有效吞吐样本**。唯一矩阵在第一个control任务 `8g-m1-default-r1` 的资源监控中止；`failure.json`记录`completed_samples=0`。未形成完整任务回执、`results.json`或`analysis.json`，不得计算或报告吞吐。按预先固定的“失败不重试”边界，本任务到此结束。

`run_guarded`在组合资源门处报`AssertionError('swap gate')`。任务开始前的R1 host `pswpout`为4,679,613页；失败与清理后读到4,679,614页，之后20秒无传输观察保持不变。异常时客户端资源状态没有写入回执，因此不能证明这次门失败只来自host计数，也不能把页面换出归因给CPNetFlux。R1 client receipt保留了任务前快照，但没有after快照或资源汇总。现有证据只支持“本轮资源门失败”，不支持性能结论或根因结论。

## 结果与清理

| 项目 | 结果 |
|---|---|
| 传输任务 | 0个完整样本；首个control任务在资源门失败。没有candidate任务或配对结果 |
| R770-2源 | 8 GiB完整驻留，2,097,152/2,097,152页；SHA-256 `32f5802da2b3b95ad39936f9297f7770e1cf9149709ad439c63aa75fc51549b6` |
| 源准备资源 | R2最低MemAvailable 21,558,444,032 B；owned swap峰值0，host `pswpout`增量0 |
| 临时服务资源 | R2最低MemAvailable 21,543,309,312 B；owned swap峰值0，host `pswpout`增量0；停止后49335端口无监听 |
| R1客户端资源 | 首次资源门断言异常；客户端owned swap峰值未能从失败证据中恢复，归因未知 |
| 空间回收 | R1输出槽、R1/R2标记tmpfs根均已移除；`cleanup.json`三项清理回执均为true。两端`/dev/shm`可用分别恢复至16,382,607,360 B和16,382,631,936 B |

本轮没有修改NIC、路由、swap、THP或系统配置。构建和运行时版本在传输前已核对为同一提交、同一RelWithDebInfo配置及相同二进制SHA-256。

## 验收与后续

两端均通过Python `-W error -m py_compile`、8项M1驱动测试、`cmake --build build -j4`及5项`ctest -R cpnetflux_large_data`。这些验证仅说明驱动和既有大文件门禁通过，不构成传输性能结果。

下一步先建立无payload的host swap事件归因记录，并修复资源门失败时客户端owned-swap/host-counter状态未落盘的问题；保持host `pswpout`增量为零的原门槛，不通过改基线或忽略计数来“修复”验收。20秒空闲稳定观察不足以证明下一轮不会发生事件。M1-41不重跑；只有事件监控和资源门证据完备后，才能另立任务评估一次新的完整M1。

证据保存在两端的`/home/Su/experiments/CPNetFlux/2026-11-11-v3-large-file-m1-41`：R1包含失败、控制任务回执、服务与清理记录；R2包含源摘要、源准备资源与服务端记录。未将payload或实验产物提交到Git。
