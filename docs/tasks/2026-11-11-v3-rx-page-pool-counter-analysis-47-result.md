# V3 接收队列 page-pool 计数分析47结果

日期：2026-11-11
代码提交：26677952feea67588b6ed040a354c24f85f824d4
输入提交：2df98f5d1f2ba093cc2d474ec0f27f8481b7e897
合同：Task47（2026-11-11-v3-rx-page-pool-counter-analysis-47.md）
状态：DONE / DIAGNOSTIC ONLY；无传输样本、无性能结论。

## 结论

只读分析器已输出逐RX队列 packet、page-pool、WQE 与 buffer allocation error delta，并明确标出接口级 rx_out_of_buffer 没有队列归属。对Task46原始快照离线复算得到全局OOB +237；rx13/rx16分别出现8/10次page-pool empty/slow事件，所有主流量队列的 buff_alloc_err、wqe_err delta为0。

这表明采样窗口内部分page-pool ring分配走过慢路径，但不能将该现象与+237接口级OOB建立因果关系，也不能归因给任一TCP lane。网络根因仍OPEN，Task46性能/profile矩阵保持中止状态。

## 实现与验证

- tools/perf/receive_network.py 新增 rx_queue_counter_deltas，识别packet、pp_alloc_fast/slow/slow_high_order/empty/refill、buff_alloc_err和wqe_err。
- 输出保留原有全局层级与 flow_cause_proven=false，另加 rx_out_of_buffer_queue_attribution=unavailable_interface_aggregate_only。
- 单测验证packet与page-pool delta、WQE/buffer错误零值，以及全局OOB不映射到队列。
- python3 tools/test/run_receive_network_test.py：7/7通过。
- python3 tools/test/run_small_file_baseline_test.py：12/12通过；tools/test/run_large_data_ab_test.py：14/14通过。
- 两份Python文件 py_compile 通过；git diff --check 通过。
- R1/R2均在该提交运行 cmake --build build -j2，均为ninja no work；两端 ctest --test-dir build -R cpnetflux_large_data --output-on-failure 均5/5通过。
- R1/R2的CMakeCache SHA-256一致：2e124b8f117ab8c1577df7044b7c425fa402440a1a358218fcd1bb8c82dcfd04。
- 两端运行时二进制SHA-256一致：server 22a2b8cc3dea1ca5fbf6c0871f4ccf7c5907642ddad9fad398ccaff4189d98b7；download client 20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c；upload client b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0。
- R1/R2 Python相关测试均通过：receive_network 7/7、small_file_baseline 12/12、large_data_ab 14/14。

## Task46快照复算

输入 results.json SHA-256：095b36d358058a5e54a61f79b3a5d4994610351c902460555a800eb917b51024。

| RX队列 | packets | pp_alloc_fast | pp_alloc_slow | pp_alloc_empty | pp_alloc_refill | buff_alloc_err | wqe_err |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 185,929 | 92,427 | 0 | 0 | 501 | 0 | 0 |
| 13 | 185,773 | 92,532 | 8 | 8 | 388 | 0 | 0 |
| 16 | 185,895 | 92,633 | 10 | 10 | 413 | 0 | 0 |
| 29 | 185,775 | 92,552 | 0 | 0 | 376 | 0 | 0 |

另有rx7/rx12/rx17/rx31各12个packet delta，page-pool及错误计数均为0。page-pool文档定义empty为pointer ring为空、因而强制slow path；slow记录order-0慢路径分配（[Linux Page Pool API](https://docs.kernel.org/networking/page_pool.html)）。这些计数不提供接口OOB的队列映射。

复算输出：/home/Su/experiments/CPNetFlux/2026-11-11-v3-profile-overhead-counter-correlation-46/task47-page-pool-reanalysis.json，SHA-256：edf18d0113708d8d208d03e8ba57a4661d9d13d34c9bddcf920eb434abbb26cb。
R1只读检查：kernel 7.0.0-38-generic；ethtool -i显示driver mlx5_core、version 7.0.0-38-generic、firmware 16.35.8002。ethtool -S暴露接口级rx_out_of_buffer，以及逐队列wqe_err、buff_alloc_err和page-pool计数；没有逐队列rxN_*out_of_buffer字段，64个队列错误计数当前均为0。未调整系统设置。

## 下一步

保持profile扰动矩阵暂停，不重试Task46。R1当前运行kernel为7.0.0-38-generic，mlx5_core版本同为7.0.0-38-generic，固件16.35.8002。当前ethtool -S暴露接口级rx_out_of_buffer，以及逐队列wqe_err、buff_alloc_err和page-pool计数；没有逐队列rxN_*out_of_buffer字段，64个队列错误计数当前均为0。下一步核对该Ubuntu内核对应的mlx5源码，确认聚合计数来源及是否存在其它只读RQ归因接口；只有找到能区分解释的观测源后，才制定一次有停止门的定向采集。不得凭现有page-pool慢路径调整系统参数或恢复性能矩阵。
