# CPNetFlux Linux 桌面端交接

日期：2026-10-10
新负责聊天：桌面端产品、UI 和本地任务代理
权威代码：深圳 /root/projects/CPNetFlux
设计输入提交：df852669d2d412e2aa3d0e0bedb52e311c7471b4
底层并行开发：00 总指挥负责 V3/V2，不与桌面聊天并发修改同一文件或 Git index。

## 用户要做的产品

用户群体是超算学生和老师，使用 Linux。桌面端让不会命令行的人创建、运行、观察、取消和恢复 CPNetFlux 传输。第一版建议支持 Linux 本机目录 ↔ 远端 CPNetFlux 节点；远端节点 A ↔ 节点 B 作为后续能力，不要把它误写成首版必需。

用户要求先完成 V3 动态目录调度和 V2 可靠能力补齐，桌面端聊天负责先完成产品设计和可运行 UI，再按 00 的串行交接安排实现。

## 当前系统事实

CPNetFlux 是 Linux/C++20 TCP 传输研究系统，不是完整 GridFTP。控制面负责认证、路径、协商和完成回复；数据面使用 CPNetFlux framed data。目录 V2 使用跨文件持久数据 session，多通道每通道一组控制 TCP + 数据 TCP，默认 pending window=2，当前按 fileId modulo N 静态分片。V2 当前限制 fresh、checksum none、compression off、scheduler off、data TLS off、每通道一条数据流；不满足条件时 payload 前回退 V1。

V1 是当前功能更完整的逐文件可靠路径，包含既有 checksum/manifest/resume 等能力。V2 会逐步覆盖它；在 V2 功能和门禁未完成前，不能删除 V1。loopback 和固定构建已有结果，但最新多通道版尚无新的深圳—上海/GridFTP 匹配性能结论。历史 100Mbps 单持久通道短测曾接近线路上限，不能当作所有版本的保证。

现有可执行程序包括 tree upload/download client 和 cpnetflux-gridftp-server。已有 JSON summary、JSONL event log，但还没有一个稳定完整的 GUI 任务 API；GUI 不应依赖解析人类化 stdout 作为事实源。

## 节点之间如何联系和认证

“安装两个客户端就自动互传”不是正确模型。至少需要：

1. 目标节点运行 CPNetFlux 服务端，并配置监听控制端口、数据端口范围和允许的 root 目录；
2. 发起端能访问控制端口和协商后的数据端口；
3. 客户端和服务端协商协议能力；
4. 用户/任务通过认证；
5. 服务端按 root-confined 路径和权限决定是否允许读写；
6. 双方版本、TLS、checksum、resume 能力不匹配时有明确拒绝或 fallback。

CPNetFlux 当前已有 anonymous/token、用户名/密码字段和可选控制 TLS 的配置，但不是完整账号、角色、配额和多租户系统。GridFTP GSI 是外部对照实验的认证，不应直接当作 CPNetFlux 用户认证。

产品化推荐分层：
- 服务端身份：TLS 证书由受信 CA 验证；
- 用户身份：短期 token 或管理员签发的用户凭据；不在日志显示；
- 授权：每个节点配置允许的 root、读/写权限和文件策略；
- UI：凭据存 Linux keyring/libsecret，JSON-RPC 和日志只返回脱敏状态；
- 连接测试：先做 DNS/端口、TLS/服务器身份、登录、能力协商、目录权限五步检查。

首版由一个 Linux 桌面端发起到一个已部署的 CPNetFlux 服务端。两台桌面端若要互传，不应让 GUI 直接互相传文件，而应让一端作为发起客户端、另一端运行受控服务端；远端到远端需要后续引入协调器或 agent-to-agent 授权。

## 建议进程和 UI

cpnetflux-desktop（Qt 6/QML）只负责界面；cpnetflux-agent 是本地任务代理，负责任务生命周期、现有 client 子进程/库、事件聚合、取消、恢复和历史。UI 与 agent 使用当前用户权限的 Unix domain socket JSON-RPC；UI 崩溃不应杀死 agent 任务。第一版 agent 可包装已有 tree client，稳定后下沉 C++ library API。

页面：
- 任务首页：新建、运行中、可恢复、历史；
- 新建任务：方向、节点、源/目标路径、冲突策略、恢复；
- 详情：文件/字节双进度、实时/平均速度、ETA、已发送/已提交、通道和实际 mode；
- 节点设置：地址、端口、认证、CA、默认 root；
- 高级：auto/1/2/4/8 channels、pending、checksum、data TLS、V2 开关，并显示约束。

JSON-RPC 最小方法：
profile.list/create/update/delete
node.testConnection
node.listDirectory
task.create/start/cancel/pause/resume/get/list
event.subscribe

task.create 必须包含 direction、source、destination、requested_mode、channels、pending_window、checksum、resume、conflict_policy。task 状态必须区分 queued/scanning/connecting/negotiating/transferring/committing/completed/failed/cancelled/recoverable，并记录 actual_mode、fallback_reason、files、bytes、speed、ETA、data_connections 和错误对象。pause 若未实现必须返回 unsupported，不能伪造成功。

## 桌面端开发限制

先做方案、接口契约和 UI 骨架，不修改底层传输文件；底层 V3/V2 由 00 串行推进。所有 UI/API 代码也在深圳权威仓库提交和 push；Windows 仅连接/查看，不是开发副本。不要触碰 /root/projects/GridFlux-Beta、/root/projects/CPSS(DCC) 或用户正在跑的实验，不复制凭据。

## 第一轮交付

1. 阅读远端 AGENTS、当前设计决策和本文件；
2. 交付桌面端信息架构、认证/节点流程、JSON-RPC schema 和 UI 状态模型；
3. 创建最小可运行 Linux Qt/QML shell 或明确 Qt 依赖/构建方案；
4. 等 00 确认 V3/V2 接口边界后，再接入真实传输任务；
5. 每阶段保留任务单、构建命令、测试和 GitHub SHA，不把 mock UI 当作真实传输完成。
