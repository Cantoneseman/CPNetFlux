# 128×1MiB 目录短实验

代码：DIR-INTEGRATION-01，控制流水线与数据连接复用已统一到深圳权威根目录当前分支。本说明中的命令在深圳执行，运行端是深圳、传输对端是上海。长实验由用户手动启动；此交付未启动跨域传输。

## 固定范围
仅 tree_dense_128MiB（128×1MiB）、fp1/n1、相同 seed=20260831、双向各3次。
CPNetFlux depth0/1/2/4 共24项，真实 GridFTP 6项，共30项。
两者数据通道均明文，CPNetFlux scheduler/compression/checksum=off/off/none、POSIX、worker control reuse；另做外部独立 SHA-256。
目录阶段计时统一开启；不加其他数据集、并发配置或 iperf 扫描。
GridFTP 匿名服务由现有 runner 在本 run 的独立目录/端口启动，核对归属后结束，不复用或停止旧2811/22310服务。

## 启动
从深圳根目录选择已提交且已验收的版本。此脚本仍要求执行源码为干净的固定提交；若根目录有历史 dirty 文件，先准备该提交的运行快照，不要回到旧任务 worktree 或清理未知文件。深圳到上海的既有 SSH 身份须能 BatchMode 登录；不要复制私钥或将凭据写入环境文件。脚本会检查资源、端口、重新构建当前提交并部署同 hash 的 server 二进制。端口或空间不满足时停止，不能自动清理旧数据。

```sh
cd /root/projects/CPNetFlux
RUN_ID="dir-depth-$(date -u +%Y%m%dT%H%M%SZ)"
LOG="/tmp/$RUN_ID.console.log"
nohup env \
  CPNETFLUX_EXPERIMENT_EXPECTED_COMMIT="$(git rev-parse HEAD)" \
  CPNETFLUX_EXPERIMENT_REMOTE=gridflux-beta-shanghai \
  CPNETFLUX_EXPERIMENT_CONTROL_HOST=47.116.174.181 \
  CPNETFLUX_EXPERIMENT_BUILD_DIR=/tmp/cpnetflux-runs/dir-integrated-depth-build \
  CPNETFLUX_EXPERIMENT_RUN_ID="$RUN_ID" \
  bash tools/experiments/gridftp_compare/run_async_control_depth_sweep.sh \
  > "$LOG" 2>&1 < /dev/null &
echo "launcher PID=$!; log=$LOG"
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

## 整合后的功能选择

- 本脚本仅比较控制流水线 depth0/1/2/4；默认数据连接复用 off。
- 持久通道一期另用 `--data-session-reuse tree --control-pipeline-depth 0`，只支持单 worker/单流、fresh、checksum none、compression/scheduler/data TLS off。
- 控制流水线与持久通道一期是独立运行模式，同时显式请求会报错，不静默丢弃任何一种。两者已在同一源码版本，不代表两种调度已组合。
- 运行 summary 必须核验实际 mode、pipeline slot/pending 或 observed data_connect_count；仅参数出现不能证明功能生效。
- 下一次正式比较须使用统一提交和对应二进制；本整合阶段没有运行跨域实验，也未修复旧 GridFTP 对照阻塞。
