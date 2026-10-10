# Linux 桌面产品与状态模型（draft-1）

## 信息架构与用户流程

| 页面 | 内容/主要动作 | 空、忙、失败状态 |
|---|---|---|
| 任务首页 | 新建上传/下载；运行中、可恢复、历史；双进度和实际模式摘要 | 首次使用引导添加节点；agent 离线显示最后快照时间，禁用变更动作 |
| 新建传输 | 方向→节点→本地文件选择器/远端浏览→冲突策略→安全与校验摘要→创建→开始 | 创建仅保存 queued，不启动 payload；不支持能力在开始前拒绝或按允许策略 fallback |
| 远端目录 | root-relative 面包屑、分页目录、名称/类型/大小/权限；只选已授权目录 | 无权不展示猜测路径；空目录与读取失败分开；不跟随逃逸 symlink |
| 任务详情 | 文件/字节、实时/平均速度、ETA、已传输/已提交、通道/队列；文件、range、attempt 子视图；取消/恢复 | totals 未知用“扫描中/未知”，不显示 0%；断开显示过期快照；错误按作用域展示 |
| 节点设置 | 地址、端口、CA/server_name、身份/凭据状态、逻辑 root、五步测试 | token 过期/密钥环锁定提供重新登录；证书错误禁止跳过验证 |
| 历史/恢复 | 已完成/失败/取消、checkpoint 资格、attempt 历史；恢复资格检查后续传 | 没有 checkpoint 或源已变化时说明不能恢复；不以重传冒充恢复 |
| 高级设置 | 自动/1/2/4/8 channel、pending、queue depth、range policy、checksum、resume、TLS 和 fallback policy | 显示本地与远端交集，未支持置灰；auto 阈值由引擎决定 |

首次用户：管理员部署服务并告知节点地址/CA/凭据→添加节点→连接测试→选择上传或下载→选目录→预览冲突/能力→创建并开始→看详情→完成或按资格恢复。
普通默认：mode=auto，scheduler=auto，range_policy=auto，checksum_policy=required_crc32c，resume_policy=allow，控制 TLS 验证必需、data TLS=required。后端若不满足安全条件拒绝，不自动关闭 TLS 或校验。现有 V2 快速限制可能导致保持保护的 V1 fallback；V1 也不满足则失败。实验 raw 必须用户主动选择并确认，不能成为产品默认。

## 身份与状态

transfer_id 是桌面任务身份，file_id + generation + relative_path + size 是不可变文件身份，range_id + generation + offset + length 是范围身份。attempt_id 唯一且 attempt_number 递增，每次重试/恢复新建 attempt，旧事件不能确认新 attempt。底层 transfer_id 另由适配映射维护，不默认等于桌面 ID。

| 实体 | 状态 | 事实与动作 |
|---|---|---|
| 任务 | queued/scanning/connecting/negotiating/transferring/committing/completed/failed/cancelled/recoverable/paused | paused 只在真实 checkpoint+暂停门通过后可用；当前默认 unsupported |
| 文件 | queued/claimed/transferring/verifying/committing/committed/failed/cancelled/recoverable/skipped | 所有适用 range verified、最终校验、manifest 与 commit 成功后才 committed |
| range | queued/claimed/transferring/received/verified/checkpointed/failed/cancelled | received 不等于 verified；checkpointed 不等于文件 committed；空 range 不创建 |
| attempt | queued/running/cancelling/paused/completed/failed/cancelled/recoverable | 取消请求后先 cancelling；最终结果可能 completed（commit 先胜出） |
| 连接 | pending/resolving/connecting/tls/authenticating/negotiating/ready/closed/failed | role=control/data，记录 channel_id；连接数是观测值，不从 channel 请求推算 |
| 认证 | unknown/not_required/required/authenticating/authenticated/expired/failed/keyring_locked | authenticated 不代表目录授权；anonymous 仅显式测试策略 |
| 恢复资格 | unknown/eligible/ineligible | 由引擎检查 checkpoint、源身份/目标策略、版本能力；UI 不能根据历史 bytes 推断 |

通常主路径 queued→scanning→connecting→negotiating→transferring→committing→completed；方向和扫描顺序由后端决定，允许 scanning/connecting 交替，UI 不硬编码步骤顺序。任何活跃阶段可失败。取消先记录取消请求，停止领取新工作，收口活跃操作并落盘，再确认 cancelled/recoverable；不得删除已提交文件。queued 可直接 cancelled。completed 的重复取消返回 already_terminal、保持 completed。resume 只接收 eligible，复核后新 attempt，原终态保留在历史。恢复总量是逻辑集合，不能按 attempt 累加 committed bytes。

主状态与恢复资格独立：cancelled/failed 仍可能 eligible；recoverable 表示已确认可恢复但尚未启动。pause unsupported 不改变任何状态；真正 paused 必须可恢复且没有活跃 payload。UI 退出仅断开订阅；agent 独立用户服务继续任务。agent 崩溃重启先重建记录并核对 checkpoint/子进程，未知时显示 unknown eligibility，绝不直接 completed 或盲目重复启动。

## 模式、计数与证据

requested_mode=auto/v1/v2；actual_mode=unknown/v1/v2。实际 scheduler=unknown/static/dynamic_ring，range policy=unknown/auto/file/range_stripe；V3 以 scheduler/range 能力呈现。通道领取共享有界队列，不给文件绑定永久 channel 或 fileId modulo。
明确请求 v2 而实际 v1 必须有 fallback_reason；auto 选择 V1 同样记录原因。原因对象包含 code/message/required/available，可记录 checksum/resume/data_tls/mode/scheduler/range 不支持、版本不匹配；fallback 只在 payload 前并保留安全策略，开始后断线进入失败/恢复，不自动重播 V1。

所有尚未观测数值为 null。零仅代表观测到零，空扫描目录可以 totals=0；ETA null 不写成 0 秒。bytes_transferred 是当前 attempt 去重逻辑字节（不等于 wire bytes）；bytes_committed 是任务跨 attempt 已提交逻辑字节，verified bytes 是恢复可用事实。实时/平均速度以 bytes/second、单调时钟由 agent 算，UI 可转换 MiB/s；total 未知不计算百分比，commit 前不显示任务完成。
记录 transfer_status/integrity_status/evidence_status 分离；completed 必须 scan_complete、总文件已 committed/skipped、files_failed=0、bytes_committed=bytes_total 及最终 commit 证据。总量未明、部分范围完成或进程 exit 0 本身不构成 completed。

## 节点认证与连接 UX

安装两个 GUI 不等于可以互传。接收节点先运行受控 CPNetFlux 服务端，配置控制监听、数据端口范围、允许 root 和读写政策；UI 不隐式启动公网接收端。

五步检查：①DNS/控制端口 ②CA 链、主机名、有效期验证 ③token/管理员凭据登录 ④版本与安全、checksum/resume/V2/V3 能力协商（包含实际协商数据端口检查） ⑤目标目录读/写权限。每步 pending/running/passed/failed/unsupported/skipped；失败后依赖步骤 skipped，不能全绿。数据端口有独立结果字段，只检查服务端此次分配端口，不扫描整段端口。若只有控制面通过但未测数据口，整体 ready=false。

root 使用逻辑 root_id 与相对路径，客户端不自行授予服务端 root；目录 UI 隐藏宿主物理路径。上传检查写授权、下载检查读授权，测试不通过写入未知文件来探权。服务器缺少权限探测则 unsupported，并在真实操作时继续由服务器授权，UI 不承诺已具备权限。

TLS 验证服务器身份，token 验证用户身份，root/权限决定授权，三者单独呈现。GridFTP GSI 只用于对照，与产品身份无关。凭据存当前用户 Secret Service/libsecret；配置/RPC 仅 credential_ref。UI 获取凭据使用专用 keyring 提示，不经通用 RPC 传 token。agent 运行时取出并以已确认安全入口交给后端；不得 argv/env/log 暴露。密钥环不可用时 fail closed，不回退明文文件。当前 anonymous/token 不扩写为完整 RBAC/配额。
