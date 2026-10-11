# V3-STABILITY-01 结果：多工况稳定性回归

日期：2026-10-11
输入提交：b3e4dc0e8468cad4d09d7d4a47c8dc69fd15c55f（codex/V1-RETIRE-01）
工作分支：codex/V3-STABILITY-01
工作树：/tmp/cpnetflux-runs/V3-STABILITY-01/src
任务范围：见 [2026-10-11-v3-stability-01.md](2026-10-11-v3-stability-01.md)

## 复现与修复

- TLS 对端 RST 后服务以 -13（SIGPIPE）退出。TLS RST 回归矩阵覆盖控制连接、TLS 握手及 TLS 数据连接；修复后每类 8 轮，每轮后验证服务存活，并在每类工况结束后验证健康 TLS 重连。
- range 恢复可正确写出数据，但 loaded_verified_chunks / removed_corrupt_chunks 未从 range coordinator 汇总。修复前，同一损坏恢复测试因 removed_corrupt_chunks=0 失败；修复后会验证 loaded_verified_chunks>0、removed_corrupt_chunks=1，并比对最终 SHA256。
- 原损坏测试固定改写 .part 文件偏移 17，不能保证该偏移属于 manifest 已验证块。现在从 manifest 首个 verified_chunks 记录派生损坏位置，保证测试确实覆盖已校验块损坏后的恢复。

## 多点工况结果

- 128×1 MiB 目录：4 通道、动态文件调度，上传与下载均通过；128 文件 / 134217728 字节，树 SHA256：f3d3334e3624617644b1f76e8f7bdf9f589526afdc4efffcd1bcf2f0317aacc5。
- mixed 256 MiB 目录：4 通道、动态文件调度，上传与下载均通过；148 文件 / 268435456 字节，树 SHA256：8b186d099796c8f95af70fc2f0686899a78de8d243158b7db96bddac7eef057d。
- 129 MiB 单文件损坏 range resume：最终 SHA256 匹配；损坏记录被移除并成功重传，仍完整的已校验块统计大于 0。
- TLS 控制面、握手、数据面异常 RST：每类 8 轮；服务保持运行，后续健康 TLS 登录成功。

## 验证

配置与构建：

    cmake -S <src> -B <build> -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON
    cmake --build <build> -j2

结果：通过。Python 测试脚本 py_compile 通过。

完整门禁：

    env CPNETFLUX_TEST_TOKEN=V3StabilityMatrixToken20261011 ctest --test-dir <build> --output-on-failure

结果：282 项通过，0 项失败；FileIoTest.IoUringContextReadWriteSmokeWhenAvailable 因当前环境 io_uring 不可用而 skip。

最终变更后的定向门禁：

    ctest --test-dir <build> -R "cpnetflux_v3_stability_matrix|cpnetflux_gridftp_control_retr_corrupt_resume_smoke" --output-on-failure

结果：2/2 通过。矩阵已注册为 cpnetflux_v3_stability_matrix CTest。

Loopback 只证明本地错误恢复和完整性工况，不代表跨域性能或网络稳定性结论。本阶段没有启动深圳—上海实验。

## 尚未完成与交接

这是一次可重复的回归加固阶段，不构成 V3 生产 readiness 结论。下一步可由 00 在代码门禁通过后安排受控短实验：先确认两端资源、TLS/GSI 配置与回滚点，再覆盖预演需要的少量真实网络工况。

回滚：对本阶段提交执行 git revert <commit-sha>。只回滚本阶段明确文件。
