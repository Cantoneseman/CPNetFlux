# DIR-ASYNC-CONTROL-02 结果

- 输入提交：`0a171f0776535f7434ddf29d16faca93e9095b87`。
- 工作区：深圳隔离 worktree `/tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-02/src`，分支 `codex/DIR-ASYNC-CONTROL-02`。
- 变更：控制循环独占控制连接读写；数据任务后台执行并经有界完成队列回报；每条 pipeline lane 最多当前任务加一个 lookahead；终态回复带 `transfer_id`；断连时取消并回收任务。默认 depth=0 保持同步路径。
- 实验入口：`tools/experiments/gridftp_compare/run_async_control_comparison.sh`。它只供用户手动运行，不在本任务启动 WAN 矩阵。
- 对比口径：dense/mixed 目录，双向、三次重复、POSIX、worker reuse、scheduler/compression/checksum off；depth=0 同批采集 GridFTP，depth=1 采集 CPNetFlux；比较器要求三次成功且双端 hash 一致，逐配置按中位 logical goodput 判定 `>=0.90`。

## 已执行验证

- Release build：`cmake --build /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-02/build --parallel 2`，通过。
- 定向 CTest：`ctest --test-dir /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-02/build --output-on-failure -R 'TreePipeline|cpnetflux_tree_pipeline'`，13/13 通过。
- Python 门禁：`python3 -B -m unittest tools.experiments.gridftp_compare.test_gridftp_compare tools.experiments.gridftp_compare.test_async_control_comparison`，33 项通过。
- Shell/CLI 门禁：`bash -n tools/experiments/gridftp_compare/run_async_control_comparison.sh`、runner/比较器 `--help`，通过。
- 未执行：深圳到上海 WAN 性能矩阵；因此没有 CPNetFlux/GridFTP 实际吞吐数据，也没有宣称达到 90%。

## 手动运行前置

脚本会拒绝 dirty worktree、缺少构建产物、磁盘不足、远端二进制 SHA-256 不一致等情况。操作者必须设置 `CPNETFLUX_EXPERIMENT_REMOTE`、`CPNETFLUX_EXPERIMENT_CONTROL_HOST`、`CPNETFLUX_EXPERIMENT_BUILD_DIR`、`CPNETFLUX_EXPERIMENT_GRIDFTP_HOME`，并确认上海新实验端口范围和 GridFTP 认证配置已经获准。脚本不接收或记录凭据。
