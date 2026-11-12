# 资源门失败证据补全 42

日期：2026-11-11
输入提交：`52c3d68011fe219d8a75dee95a1b0b543700d0e6`
状态：完成。提交 `57a95626503390c529d2608de2c8c93c9aff3172` 已在R1、R2以同一源码和二进制验证；不运行网络传输。

## 范围

当`run_guarded`因观察器、超时、内存门或swap门异常终止自有子进程时，保存失败原因、退出码、任务时长、内存低水位、owned swap采样、host `pswpout`前值/后值/增量及采样数。让`client_sample`在清理前把失败与资源侧车写入任务receipt，并在receipt中记录输出槽清理结果。

## 非目标

- 不改资源门阈值，不把host swap变化排除出验收。
- 不改传输运行时、网络/NIC配置或实验矩阵。
- 不启动或重试任何传输，不产生性能结论。

## 文件与验收

修改`tools/perf/run_small_file_baseline.py`，扩展已注册CTest `cpnetflux_small_file_baseline_driver_test`对应的`tools/test/run_small_file_baseline_test.py`，并更新本任务结果。无需改CMake。

测试用受控子进程触发observer/resource异常，确认子进程被回收、资源失败侧车包含原始错误与终态计数，客户端receipt保留失败状态且精确输出槽已删除。运行Python `-W error -m py_compile`、该Python回归测试、`cmake --build build -j4`及对应CTest；不得运行网络传输。

结果证据：[任务42结果](2026-11-11-v3-resource-gate-failure-evidence-42-result.md)。下一步为无payload host swap事件归因；M1-41不重跑。
