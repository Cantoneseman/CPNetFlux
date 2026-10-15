# CPNetFlux 演示录制脚本

日期：2026-10-15
输入版本：V3-WORKER-STATE-01C，实验提交 `cd11a47d9e10620019fc84dd0610e104c6da9f41`

这次录制分为两个独立部分：

1. R750 图形会话中的软件 UI，证明安装、节点配置、真实目录任务、取消/恢复和校验状态。
2. 云端 SSH 命令行中的性能展示，使用同一数据集分别运行 CPNetFlux V3 01C 和 GridFTP GSI。

两部分不要混用结论。R730/R750 的链路层是 100GbE；原生 verbs 短测约为单向 59--60Gbps、双向合计约 114Gbps。CPNetFlux 01C 当前没有 `libibverbs/rdma_cm` 数据面，实际使用 RoCE 网口承载的 TLS/TCP framed path。性能视频中只展示软件实际输出。

## 第一部分：R750 UI

### 录制前准备

在 R750 的 GNOME 会话中打开终端，准备一个只用于演示的本机 loopback CPNetFlux 服务。服务使用本机目录作为授权 root，节点地址填 `127.0.0.1`，因此这部分不宣称跨域传输。

```bash
set -e
DEMO=/tmp/cpnetflux-ui-recording
ROOT="$DEMO/server-root"
CRED="$DEMO/credentials"
SRC="$HOME/Videos/cpnetflux-ui-source"
rm -rf "$DEMO" "$SRC"
install -d -m 700 "$ROOT" "$CRED" "$SRC"

openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
  -keyout "$CRED/server.key" -out "$CRED/ca.pem" \
  -subj '/CN=cpnetflux-ui-demo' \
  -addext 'subjectAltName=DNS:cpnetflux-ui-demo,IP:127.0.0.1'
cp "$CRED/ca.pem" "$CRED/server.pem"
chmod 400 "$CRED/server.key"
umask 077
head -c 48 /dev/urandom | base64 -w0 > "$CRED/token"
chmod 600 "$CRED/token"

# 目录任务必须有可观察的传输时间；这里生成约 256MiB、148 文件的混合目录。
python3 - "$SRC" <<'PY'
import hashlib, pathlib, sys
root=pathlib.Path(sys.argv[1])
spec=[*(('tree-mixed/file-%04d.bin'%i,512*1024) for i in range(128)),
      *(('tree-mixed/file-%04d.bin'%(128+i),4*1024*1024) for i in range(16)),
      *(('tree-mixed/file-%04d.bin'%(144+i),32*1024*1024) for i in range(4))]
for rel,size in spec:
    p=root/rel; p.parent.mkdir(parents=True,exist_ok=True)
    left=size; n=0
    with p.open('wb') as f:
        while left:
            b=hashlib.sha256(f'ui-demo:{rel}:{n}'.encode()).digest()*128
            b=b[:min(left,len(b))]; f.write(b); left-=len(b); n+=1
PY

cpnetflux-gridftp-server \
  --host 127.0.0.1 --port 24522 --root "$ROOT" --data-port-base 24620 \
  --connections 4 --chunk-size 1048576 --buffer-size 1048576 \
  --checksum crc32c --checksum-backend auto \
  --manifest-flush-policy every_n_chunks --manifest-flush-interval-chunks 16 \
  --final-verify-policy full --commit-sync-policy fsync_file --preallocate full \
  --auth-mode token --auth-token-file "$CRED/token" --access-policy read-write \
  --tls-mode required --tls-cert-file "$CRED/server.pem" \
  --tls-key-file "$CRED/server.key" --tls-ca-file "$CRED/ca.pem" \
  --data-tls-mode required > "$DEMO/server.log" 2>&1 &
echo $! > "$DEMO/server.pid"
systemctl --user restart cpnetflux-agent
```

服务器启动后打开 UI：

```bash
cpnetflux-desktop --socket "$XDG_RUNTIME_DIR/cpnetflux/agent.sock"
```

### 镜头顺序与台词

1. **安装完成**：展示应用版本、`cpnetflux-agent.service` 为 active、桌面端连接状态。说明“当前是真实 agent，不是 `--demo` 合成数据”。
2. **添加节点**：节点名填“R750 本机演示节点”；地址 `127.0.0.1`；端口 `24522`；server name `cpnetflux-ui-demo`；CA 选择 `/tmp/cpnetflux-ui-recording/credentials/ca.pem`；认证方式 token；凭据引用填写 `file:/tmp/cpnetflux-ui-recording/credentials/token`；root id 填 `ui-demo-root`。不要在 UI 或镜头中展示 token 内容。
3. **连接检查**：点击“分步连接检查”。逐项解释 DNS/控制端口、TLS 身份、登录、能力、数据端口和目录权限分别代表什么。只读出界面事实：如果某一步为 `unsupported` 或 `unknown`，说明“当前 adapter 没有该项结构化证据”，不要说成认证成功。
4. **创建真实目录任务**：方向选择上传；本地目录选择 `~/Videos/cpnetflux-ui-source`；远端路径填写 `recording/session-01`；校验保持 CRC32C，TLS/data-TLS 保持 required，resume 保持 allow。创建后先展示 `queued`，再点击“开始任务”。
5. **传输中**：展示扫描、连接、协商、传输、提交状态变化；详情页展示文件数、逻辑字节、实际模式、checksum、manifest/finalize 和阶段耗时。台词：“这些字段来自 agent 回读的 summary、event log 和 manifest，不是 UI 自己推算。”
6. **取消并恢复**：在 `transferring` 阶段点击“取消任务”，等待最终状态为 `cancelled` 或 `recoverable`；若界面给出恢复资格 `eligible`，点击“恢复任务”，展示 attempt number 递增和缺失内容继续处理。若资格为 `unknown/ineligible`，按界面事实说明“本次不允许恢复”，不要用重新创建任务冒充 resume。暂停按钮保持禁用，并说明“暂停尚未由后端提供”。
7. **完成与校验**：让任务进入 `committing` 再到 `completed`。展示 `transfer_status=pass`、`integrity_status=pass`、checksum、manifest 和 finalize 证据，以及文件/字节闭合。台词：“完成表示扫描、提交和完整性证据齐全，不只是进程退出码为 0。”
8. **错误边界**：可选地打开一个不存在的本地路径或错误 CA，展示明确错误；不要用错误操作制造“成功”。

录制结束：

```bash
kill "$(cat /tmp/cpnetflux-ui-recording/server.pid)" || true
rm -rf /tmp/cpnetflux-ui-recording "$HOME/Videos/cpnetflux-ui-source"
```

## 第二部分：云端 CLI 性能

### 结论边界

云端性能录制不使用 R730/R750 的实验链路。现场说清楚：这是云端网络和磁盘条件下的 CPNetFlux/GridFTP 对照；不代表 RoCE 100GbE 的峰值，也不代表跨域生产结果。

### 推荐录制顺序

1. 在云端两端先展示 `hostname`、磁盘余量、CPNetFlux 版本 SHA、GridFTP GSI 服务端口和 GSI proxy 剩余时间。
2. 展示 CPNetFlux 认证/能力预检，确认 TLS 身份、token login、能力协商和数据端口均通过。
3. 先跑一个 256MiB 单文件 smoke，展示实时命令输出和 JSON summary。
4. 再跑三个固定工况各 1 次作为现场短演示：`single_256MiB`、`tree_dense_128MiB`（128×1MiB）、`tree_mixed_256MiB`（148 文件、256MiB）。每个工况分别执行 upload/download，并紧接着执行同工况 GridFTP GSI upload/download。
5. 最后展示汇总表。完整实验使用 3 次重复；现场录制使用 1 次即可，避免把视频时间和云端资源耗尽。

CPNetFlux 命令中的关键参数固定为：

```text
connections=4
file-parallelism=4
checksum=crc32c
control-reuse=worker
data-session-reuse=tree
file-scheduling=dynamic
data-pending-window=2
control-pipeline-depth=2
scheduler=off
compression=auto
TLS/data-TLS=required
phase-timing=on
```

GridFTP GSI 命令固定使用：

```text
globus-url-copy -fast -dcpriv -cd -rp -r -p 8 -cc 8 -tcp-bs 1048576
```

视频中要指向实际输出字段：CPNetFlux 的 `actual_mode=persistent_tree`、`actual_file_scheduling=dynamic`、`control_pipeline_actual_mode=shared_persistent_dispatch`、`checksum_algorithm=crc32c`、`result=pass`；GridFTP 则展示 GSI proxy、传输耗时、文件数/字节数和哈希校验。不要把 `ib_write_bw` 的 Gbps 作为任一软件吞吐。

### 性能展示台词

“这里比较的是同一数据集、同一方向、同一重复次数下的有效样本。CPNetFlux 的优势重点看密集目录和混合目录，因为动态文件队列、持久 worker 和共享工作租约减少了小文件调度和重复建连开销。单文件结果只用于说明大文件路径，不据此宣称已经使用 RDMA verbs。”

### 现场故障处理

- 认证预检失败：停止扩大工况，只录错误和修复动作。
- 任一正式样本失败：停止矩阵，不剪辑成成功；保留错误日志和 summary。
- 云端磁盘不足：清理本次 payload 和远端 case 目录，不删除已有 GridFTP 服务目录。
- 传输结果字段为 `unknown`：按未知展示，不填 0、不补写成功。

## 验收输出

- UI 录制：安装、真实 agent、节点 profile、目录任务、取消/恢复资格、completed 校验和错误边界。
- CLI 录制：版本/认证预检、CPNetFlux smoke、GridFTP GSI smoke、三类工况短矩阵和汇总。
- 交付报告：记录云端主机、版本 SHA、参数、样本数、有效性、吞吐中位数和未完成项。
