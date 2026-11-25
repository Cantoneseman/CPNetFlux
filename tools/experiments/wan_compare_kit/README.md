# CPNetFlux 与 GridFTP 对比测试套件

在两台机器之间对比三种传输：

| 名称 | 含义 |
|---|---|
| `gridftp` | globus-url-copy 对 globus-gridftp-server |
| `cpn_raw` | CPNetFlux V3，关闭压缩 |
| `cpn_adaptive` | CPNetFlux V3，`--compression auto --compression-policy adaptive` |

数据集有三种：`big_text`（CSV 大文件，可压缩）、`big_random`（随机大文件，不可压缩）和
`small_text`（大量 64 KiB 的 CSV 小文件）。每个数据集测上传和下载两个方向，每次传输后都用
sha256 校验，只有校验通过的结果才计入中位数。

## 不影响机器日常使用

- 所有文件只写在 `KIT_HOME`（客户端）和 `REMOTE_KIT_HOME`（服务端）下面；删除操作会先检查
  路径是否在这两个目录内。
- 不需要 root，不安装软件，不改系统配置、防火墙或网络。GridFTP 和 CPNetFlux 服务端以当前用户
  身份运行，使用 kit 自带的测试 CA 和证书（不碰 `/etc/grid-security`），只监听配置里指定的端口。
- CPNetFlux 从 `CPN_COMMIT` 用 `git archive` 导出构建，不修改、不切换你的工作目录。
- 服务进程按 pid 文件管理，停止前会核对进程命令行，不会误杀其他进程。
- 构建和服务进程以 `nice -n $NICE` 运行，`BUILD_JOBS` 默认 4。
- `preflight.sh` 只读检查端口占用和磁盘空间，发现冲突会报错，不会抢占端口。
- 测试会占满链路带宽。请在机器空闲时运行，并先和使用这台机器的人确认。

## 前提条件

| 位置 | 需要 |
|---|---|
| 客户端 | `globus-url-copy`、`grid-proxy-init`、`openssl`、`python3`、`git`、`flock`、`timeout`；需要构建时还要 `cmake`、`g++` |
| 服务端 | `globus-gridftp-server`、`python3`、`sha256sum`；需要构建时还要 `cmake`、`g++` |
| 两端（可选） | `iperf3`，用来测链路基线 |
| 客户端到服务端 | 免密 SSH（`SERVER_SSH`），以及配置里的端口在防火墙中放行 |

Ubuntu 上 Globus 相关命令来自 `globus-gass-copy-progs` 和 `globus-gridftp-server-progs` 两个包。
没有安装时请联系机器管理员；本套件不会替你安装。

## 使用步骤

在客户端的仓库目录中执行：

```bash
cd tools/experiments/wan_compare_kit
cp kit.conf.example kit.conf      # 然后编辑 kit.conf，至少检查服务端地址、SSH、目录和端口
./preflight.sh                    # 只读检查，输出 "preflight ok" 才继续
./setup_certs.sh                  # 生成 kit 专用的测试 CA、证书和 token，并复制到服务端
./build_cpnetflux.sh              # 两端分别构建 CPN_COMMIT，并打印两端二进制的 sha256
./servers.sh start                # 在服务端启动 kit 的 GridFTP 和 CPNetFlux 服务
./bench.sh                        # 跑全部组合；也可以 ./bench.sh <结果名>
./servers.sh stop                 # 测完立即停止服务
./cleanup.sh                      # 删除两端的测试数据；加 --all 会把 kit 目录整个删掉
```

`kit.conf` 已加入 `.gitignore`，不要提交它。要换配置文件，可以用 `KIT_CONF=/path/to/my.conf ./bench.sh`。

结果在 `$KIT_HOME/client/results/<结果名>/` 下：

- `summary.md`：中位数表（括号内是"校验通过次数/总次数"）、自适应压缩的压缩比和决策原因、iperf 基线，以及失败项列表。
- `results.csv`：每次传输的原始记录，包括网卡收发字节差。
- `env.txt`：commit、两端二进制的 sha256、路由和完整配置。
- `<工具>_<数据集>_<方向>_r<轮次>.log`：失败时先看这个日志。

`summary.md` 可以随时重新生成：`python3 summarize.py <结果目录>`。

## 在 R770-1 和 R770-2 之间测试

`kit.conf.example` 的默认值就是这个场景：客户端 R770-1，服务端 `Su@r770-2`，走 RDMA 网卡直连
地址 `192.168.100.2`（数据面为 TCP，不是 verbs）。`bench.sh` 会记录路由和网卡计数，用来确认
流量走的是直连网卡。链路在 10 Gbit/s 以上时，256 MiB 的文件太小、测不准，建议把 `BIG_MIB` 调到
4096 或更大，并按磁盘空间调整 `SMALL_COUNT`。

如果服务端在 NAT 后面（例如云主机），需要做两处修改：

- `SERVER_HOST` 填客户端能访问到的公网地址，`GFTP_DATA_INTERFACE` 也填公网地址。
- 客户端在 NAT 后面时设置 `CLIENT_BEHIND_NAT=1`。

## 已知问题和注意事项

- **GridFTP 下载文件被写坏**：Globus 10.x 搭配 OpenSSL 3 时，会把一个 TLS 1.3 的握手后报文写进
  下载的文件。kit 已经把 GSI 限制在 TLS 1.2（`GLOBUS_GSSAPI_MAX_TLS_PROTOCOL=TLS1_2_VERSION`）。
- **GridFTP 递归传输出错**：`-r` 在下载时会报 "Bad MLSD response"，上传时传几个文件就卡住。kit
  改为用 `-f` 传显式的文件列表。
- **数据端口耗尽**：客户端在 NAT 后面时，下载走 stream 模式，每个文件占用一个服务端数据端口，关闭后
  这个端口还要在 TIME_WAIT 里停留 60 秒。此时 `GFTP_DATA_PORTS` 的范围要大于 `SMALL_COUNT`，并且
  整段都要在防火墙中放行。
- **NAT 与 MODE E**：GridFTP 的 MODE E（`-fast -p`）由发送方主动建立数据连接。客户端在 NAT 后面时
  服务端连不回来，所以 `CLIENT_BEHIND_NAT=1` 时下载会改用 stream 模式，并在结果中如实体现。
- **端口**：CPNetFlux 服务端会使用从 `CPN_DATA_PORT_BASE` 开始的最多 512 个数据端口。
- **只有上传方向支持压缩**：截至 f72f2f2，CPNetFlux 只有上传方向实现了压缩，所以 `cpn_adaptive`
  在下载方向的表现与 `cpn_raw` 相同。
- **自适应压缩可能自动关闭**：在高速链路或 CPU 较忙时，压缩可能成为瓶颈，控制器会切换到更快的
  编码或直接关闭压缩，这属于正常行为；`summary.md` 里的决策原因会写明（例如 `cpu_bound`、`low_ratio`）。
- **认证失败不是性能结果**：`bench.sh` 每次运行前都会检查 GSI proxy 的有效期，不足 4 小时就自动
  续签。GridFTP 报 530 或证书错误时，应先修复认证，再看性能数据。
- **服务端以 root 运行**：GridFTP 默认拒绝映射到 root 的用户。`servers.sh` 检测到 root 时会加上
  `-allow-root` 并给出提示，但仍建议用普通用户运行。

仓库里的 `tools/experiments/gridftp_compare` 是 R770 年度对比用的 Python 实验框架；本套件不依赖它，
只需 bash 和 python3，目标是让人在两台机器上按步骤直接跑出可复现的对比结果。

## 脚本一览

| 脚本 | 作用 |
|---|---|
| `kit.conf.example` | 配置模板，每项都有注释 |
| `preflight.sh` | 只读检查两端的工具、端口、磁盘空间和路由 |
| `setup_certs.sh` | 生成 kit 专用的 CA、主机/用户证书、CPNetFlux TLS 证书和 token |
| `build_cpnetflux.sh` | 两端构建同一 commit（构建后 strip），记录 sha256；设置 `CPN_BIN_DIR` 时跳过构建 |
| `servers.sh start\|stop\|status` | 管理服务端的两个服务 |
| `bench.sh [结果名]` | 生成数据、跑 iperf 基线和全部传输组合、校验结果并汇总 |
| `summarize.py <结果目录>` | 根据 `results.csv` 生成 `summary.md` |
| `cleanup.sh [--all]` | 停止服务并删除测试数据 |
