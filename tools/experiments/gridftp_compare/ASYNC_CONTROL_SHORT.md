# 128×1MiB 目录短实验

代码：DIR-ASYNC-CONTROL-03，分支 codex/DIR-ASYNC-CONTROL-03。本说明中的命令在深圳执行，运行端是深圳、传输对端是上海。长实验由用户手动启动；此交付未启动跨域传输。

## 固定范围
仅 tree_dense_128MiB（128×1MiB）、fp1/n1、相同 seed=20260831、双向各3次。
CPNetFlux depth0/1/2/4 共24项，真实 GridFTP 6项，共30项。
两者数据通道均明文，CPNetFlux scheduler/compression/checksum=off/off/none、POSIX、worker control reuse；另做外部独立 SHA-256。
目录阶段计时统一开启；不加其他数据集、并发配置或 iperf 扫描。
GridFTP 匿名服务由现有 runner 在本 run 的独立目录/端口启动，核对归属后结束，不复用或停止旧2811/22310服务。

## 启动
进入已提交且干净的隔离 worktree。深圳到上海的既有 SSH 身份须能 BatchMode 登录；不要复制私钥或将凭据写入环境文件。脚本会检查资源、端口、重新构建当前提交并部署同 hash 的 server 二进制。端口或空间不满足时停止，不能自动清理旧数据。

```sh
cd /tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/src
RUN_ID="dir-depth-$(date -u +%Y%m%dT%H%M%SZ)"
nohup env \
  CPNETFLUX_EXPERIMENT_REMOTE=root@47.116.174.181 \
  CPNETFLUX_EXPERIMENT_CONTROL_HOST=47.116.174.181 \
  CPNETFLUX_EXPERIMENT_BUILD_DIR=/tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/build \
  CPNETFLUX_EXPERIMENT_RUN_ID="$RUN_ID" \
  bash tools/experiments/gridftp_compare/run_async_control_depth_sweep.sh \
  > "../evidence/$RUN_ID.console.log" 2>&1 < /dev/null &
echo "launcher PID=$!; log=../evidence/$RUN_ID.console.log"
```

默认 CPNetFlux 控制端口21210..21221、被动数据窗口34000..34522；独立 GridFTP 控制22410、数据35000..35511。
脚本先检查端口空闲；云安全组/防火墙还须允许这些既有实验用途的端口，本次未修改网络配置。
可通过 CPNETFLUX_EXPERIMENT_CPNETFLUX_CONTROL_PORT_BASE、CPNETFLUX_EXPERIMENT_CPNETFLUX_DATA_PORT_BASE、CPNETFLUX_EXPERIMENT_GRIDFTP_CONTROL_PORT、CPNETFLUX_EXPERIMENT_GRIDFTP_DATA_PORT_BASE 显式改成获准空闲范围。
不支持在本短脚本切到 GSI 私密数据通道直接宣称同配置对比；这需要另行统一安全配置。

## 输出与判断
默认输出 /tmp/cpnetflux-runs/dir-async-control-evidence/<RUN_ID>/。
- comparison.md / comparison.csv：每方向、每depth相对GridFTP和depth0的结果。
- comparison.repeats.csv：全部30项的原始吞吐、runner wall/128摊销耗时、control_prepare和transfer_complete_wait。
- experiment-input.txt、build-manifest.txt、CMakeCache.txt：commit、源码archive SHA-256、客户端/两端server SHA-256、配置与资源快照。
- depth0-gridftp、depth1、depth2、depth4：命令、结果、计划、hash与客户端summary。
- complete.txt 只表示数据收齐；失败/缺项时比较器返回非零，不能按最高观测repeat推断完成。

以同方向depth>0的wall/128中位数低于depth0作为短实验改善证据；并列GridFTP比例，不把代码测试或COMPLETE视作达到90%。
runner wall包含准备/独立校验/收尾，与客户端阶段累计时间不同；不相加或混用吞吐分母。每组只有3次，保留全部值，不作强因果结论。
当前执行顺序为depth0与GridFTP交替后依次1/2/4；时间漂移可能影响结果，若差异接近噪声再做有针对性的复核。
未见文件摊销耗时下降前不扩大矩阵。脚本没有自行删除远端历史数据，保留独立peer二进制；现有runner清理仅限本case可再生payload。
