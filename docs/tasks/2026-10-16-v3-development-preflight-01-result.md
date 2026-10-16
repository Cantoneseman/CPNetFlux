# V3-DEV-PREFLIGHT-01：开发前检查结果

- 时间：2026-10-16 23:34 至 2026-10-17 凌晨（UTC+08:00）。
- 输入提交：0f7009bb8367f2484515c2d0562ec73a21112c1e。
- 预检任务提交：fd8d0c00cc6f88088276e4d30b062bb4b3346f36；push 返回 0，GitHub main 实时回读相同 SHA。
- 结论：具备 V3-WORK-UNIFY-01 核心开发条件；跨主机/性能实验另须准入。本轮未修改产品源码或实现执行器。

## 当前规则

深圳 AGENTS.md 与云端优先决策的 2026-10-09 修订已明确：00 在 /root/projects/CPNetFlux 根目录单人串行、沿用当前分支。旧强制 worktree、并行派单和迁移前置门已覆盖；取消门禁不表示历史迁移验收通过。README/旧角色 prompt/入口和 BOARD 部分描述落后，执行以当前 AGENTS、修订决策和 2026-10-16 路线为准；Windows 只连接/查看/回收，不覆盖深圳源码。

## SSH、资源和既有服务

| 项目 | 深圳 | 上海 |
|---|---|---|
| Windows SSH | BatchMode/严格主机密钥检查成功，返回 0 | 同左 |
| 主机 | iZwz9bgztwf1tic26q48pjZ | iZuf6ja2uqvjzh635cugzmZ |
| 根盘与 /tmp | 同盘，99G 总量、26G 已用、69G 可用、27% | 同盘，99G 总量、13G 已用、82G 可用、14% |
| available 内存快照 | 约 2.5 GiB，无 swap，2 CPU | 约 2.9 GiB，无 swap |
| CPNetFlux | PID 2826455，2121，systemd active/running | PID 1500623，2121，systemd active/running |
| GridFTP | PID 2493350，22310 | PID 339900，2811 |

上海 GridFTP 仍引用 /tmp/gridflux-gridftp-compare/logs/server-gsi-secure.log。现有服务 PID/监听前后无变化，本轮没有改服务配置、部署、重启或停止服务；没有清理历史目录或其他项目。本次新运行目录约 35 MiB，低于 6 GiB 预算，保留空间高于 10 GiB。此前深圳 22G 可用快照已过时，以本次 69G 实测为准。

## 网络、认证和实验边界

- 深圳 -> 上海 2121：TCP/TLSv1.3 成功，用现有 CA 校验证书/地址并收到 220 欢迎消息；未登录、未传文件。深圳 -> 上海 22/2811 TCP 可达。
- 上海 -> 深圳 22/22310 TCP 可达；2121 TCP 建连超时。Windows -> 深圳 2121 未连通，Windows -> 上海 2121 可达。
- 深圳本机 127.0.0.1:2121 可达，监听为 0.0.0.0:2121；iptables INPUT policy ACCEPT，读取的 nft 规则未显示限制。尚不能确定是外部安全组、路由还是其他网络规则；未修改防火墙。
- 不阻塞深圳本机开发。深圳客户端/上海对端的控制通路已检查；完整认证、数据端口、上传/下载仍未验收。若需上海主动连接深圳或 Windows 直连深圳 2121，先另行核查。
- 两端公开服务器证书当前有效，notAfter 均为 2026-11-14 08:29:37 UTC。
- 两端 /tmp/x509up_u0 不存在，SSH 环境无 X509_USER_PROXY/X509_USER_CERT；已检查的 GridFTP 运行目录未找到有效 proxy。未刷新凭据；真实 GridFTP 比较前必须创建/刷新并验证 proxy 有效期、认证和 data TLS。TCP 可达不等于 GSI 可用。
- 深圳仅见 lo/eth0，无 infiniband 设备；不构成十物理链路、RDMA 或 100Gbps 验收。
- 在线已安装服务二进制 SHA 与本次构建不同，不把在线服务行为归于当前构建。

## Git 和已有状态保护

初始 HEAD == main == 本地 origin/main == GitHub main，均为输入 SHA；remote 是 git@github.com:Cantoneseman/CPNetFlux.git。git fsck --connectivity-only --no-dangling、git diff --check 返回 0；初始 index 为空，未发现顶层 Git lock 或相关编译/CTest/Git 活动进程。

以下三个既有 dirty 文件 SHA-256 前后不变，未提交本次备份：

- docs/coordination/BOARD.md：c313ebae620b1ec0288ecf7a25dcca5df08a1ea54266bdece910ad361f2ad1cd
- docs/tasks/2026-10-14-v3-worker-state-01b-result.md：79b132c9ec3b35df9a47fe87d74619d86d38a182baadfca661e253878956cbd2
- docs/tasks/2026-10-14-v3-worker-state-01b.md：6c8a906521587e825b3e8fbb205d81d97969e8a325b926c1cf46d8adc63f5aba

BOARD 仍主要描述 2026-10-14 进展，后续另行整合已有改动；下一阶段执行依据为 2026-10-16 路线及新实现任务单。本轮四个既有 worktree status 均干净，但 HEAD 均非 main 祖先，保留不合并/删除：CPNetFlux-ui-demo-03、CPNetFlux-worktrees/agent-live-progress-01、desktop-ui-reliability-01、desktop-ui-run-fix-01（完整绝对路径见 evidence）。

## 构建和测试

运行根目录：/tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/。

- 工具：Git 2.34.1，CMake/CTest 3.22.1，Ninja 1.10.1，G++ 11.4.0，Python 3.10.12，OpenSSL 3.0.2。
- 系统 spdlog/GTest/OpenSSL/zlib/liburing 开发包可用。liblz4 pkg-config 查询失败，但当前 CMake 未要求它；未安装新依赖。深圳无 rg，源码搜索使用 Python 替代。
- Release、BUILD_TESTS=ON、TLS=ON、IO_URING=ON、BUILD_DESKTOP=OFF；CMake 识别真实 liburing/OpenSSL，配置返回 0。
- Ninja 126/126 步通过，约 99.5 秒；全量已注册核心 CTest 299 项、0 失败、约 74.0 秒；原始 GTest 日志 0 SKIPPED，io_uring 读写 smoke 实际通过。不沿用旧的 300 项/1 skip 数字。
- V3 stability matrix 通过：persistent TLS/CRC32C/dynamic range/resume、128 MiB dense、256 MiB mixed，以及 control/data/handshake RST 8 轮。
- 未构建 Qt 桌面目标，核心全量 CTest 不表示桌面、跨域或十链路验收。
- 347 个 tracked 源码/测试/工具/配置文件 SHA-256 前后一致；未修改产品源码。临时测试 token 仅在进程环境注入，输出日志脱敏，不保存值。

命令及参数见任务单；运行脚本、各步骤返回码/耗时、二进制 SHA 和完整日志保留在证据目录。

## 工具、证据和备份

本轮专用聊天工具已暴露，shell/SSH 实际调用成功；codex 0.162.0-alpha.17.2 的 exec resume --help 返回 0。本轮没有向其他聊天派单/发消息、未续接角色、未建子代理；工具存在不等于角色已执行。

证据：input-snapshot.json、preflight-observations.json、build-test-summary.json、configure.log、build.log、ctest.log、test_inventory.log、run-baseline.py、evidence-manifest.json；原始 GTest 输出在 build/Testing/Temporary/LastTest.log。大日志/二进制不进 Git。最终 commit、push 返回码、远端 SHA 与本地副本校验记录在外部 completion-receipt.json。

本次仅提交预检任务、此结果和 V3-WORK-UNIFY-01 实施任务准备。后续按 docs/tasks/2026-10-16-v3-work-unify-01.md 先收敛共同契约；V3-WORK-UNIFY-02 再合并 worker loop。统一 executor 回归完成前不启动十链路性能实验；反向 2121/GSI proxy 缺口在需要对应路径的实验准入时处理。
