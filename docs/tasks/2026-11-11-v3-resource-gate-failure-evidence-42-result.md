# 资源门失败证据补全 42 结果

执行日期：2026-11-11。计划：[任务42](2026-11-11-v3-resource-gate-failure-evidence-42.md)。
输入提交：`52c3d68011fe219d8a75dee95a1b0b543700d0e6`。

## 结论

资源观察器抛出异常时，驱动现在先回收自有子进程，再把失败原因和退出状态写入客户端日志旁的 `*.resource-failure.json` 侧车。记录包括子进程退出码、任务及观察器时长、MemAvailable低水位、owned swap当前值/峰值、host `pswpout`前值/后值/增量及观察样本数。host swap增量门槛保持为零。

`client_sample`现在在失败receipt中保存原始异常、资源侧车及其路径、失败后快照，并在精确清理输出槽后写入 `output_slot_removed`。任务异常仍会向上抛出，资源门与实验判定没有放宽。

## 验证

- `python3 -W error -m py_compile tools/perf/run_small_file_baseline.py tools/test/run_small_file_baseline_test.py`：通过。
- `python3 tools/test/run_small_file_baseline_test.py -v`：9/9通过，包括受控observer异常、swap门异常、子进程回收和失败receipt清理。
- 新增receipt回归在实现前按预期以缺失 `error` 字段失败；实现后通过。
- `cmake --build build -j4`：通过，Ninja报告无待构建目标。
- `ctest --test-dir build -R cpnetflux_small_file_baseline_driver_test --output-on-failure`：1/1通过。
- `git diff --check`：通过。

测试使用短生命周期受控子进程与/dev/shm标记临时目录；临时目录由测试框架自动清除。本任务没有启动网络传输，没有生成payload，也没有新的吞吐结果。没有改CMake、传输运行时、主机资源阈值或网络配置。

## 下一步

在两端固定提交验证完成后，进行一次无payload host `pswpout`归因诊断：并行记录host计数、CPNetFlux自有进程swap、可见进程 `VmSwap`及叶cgroup swap变化，确认计数增量是否伴随可归属的进程/服务；不改swap配置、不生成传输负载。M1-41不重跑。诊断若无法归属事件，报告证据限制，并在新的任务合同中决定如何处理资源门复验。
