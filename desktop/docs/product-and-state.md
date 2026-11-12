# 桌面端开发边界与状态

本阶段唯一运行引擎为 V3，IPC 使用 API 3.0.0。API 2.x 的模式选择、
V1 fallback 和“V3 只是调度选项”的旧产品描述失效。
字段、方法和表单默认值以 rpc-contract.md、JSON schema、
agent.getInfo 和 agent.getContract 为准。

## 第一版可以开发的页面

- 节点配置和本地凭据文件引用、真实控制面连接检查。
- 本机网卡检测、推荐或手选源地址、确认保存、查询、检查和清除。
- V3 目录上传/下载表单，启动前预检和解析后的配置展示。
- 任务列表、启动、取消、具备检查点时恢复、事件断线重同步。
- attempt 历史、传输结束后的文件/range 观测明细和核心证据。

暂停、远端目录浏览、单文件统一认证、系统网卡/路由修改、逐链路远端
地址映射、实时逐块速率暂未提供。按 getContract 禁用对应入口。
不能把远端 profile 对应的本机网卡配置画成“已读取远端网卡”。

## 状态和显示原则

所有任务 requested_mode=v3；actual_mode 只为 unknown/v3。
创建时 queued，实际执行可能经过 scanning/connecting/transferring；
只把后端 completed 当完成，不根据百分比自行完成。
effective_policy 在最终成功摘要前保持 unknown，不显示成已生效。

UI 退出只断订阅。断线标 stale，重连通过 epoch/sequence 同步；
attempt 身份不同的旧消息不能覆盖当前 attempt。
cancel 请求不等于进程已停止；只有 quiescence=confirmed 才显示已取消。
resume 只在后端返回 eligible 和 checkpoint_id 时可点，
源数据和 manifest 的最终一致性由 V3 核心在恢复时检查。

网卡方案只保存 CPNetFlux 源地址。应用前显示清单版本和完整性，
用户确认后调用 apply；更改/清除时遵守错误与并发保护。
verify=true 只表示本机地址可用，不能显示为“多物理链路验证通过”。

总量未知时不用 0 代替；无实时速度时不虚构曲线。
file/range 页面依 observation/coverage 标明最终观测范围；
恢复跳过的数据可能不出现在本次 file_timings 中。
bytes_committed 是跨恢复的逻辑完成字节，不能把各 attempt 直接相加。

连接检查的 TLS/token 成功不等于远端目录授权和数据端口已验收；
ready=false 时应展示各检查项事实。管理员凭据/密钥环当前不接入，
不要设计点击后才发现后端不支持的默认主流程。

## 后续工作

桌面端按 API 3.0.0 开发视图和交互，再使用真实 agent 集成验收。
完善目录浏览、暂停、实时观测或系统网络配置属于独立后端功能，
需要新的核心能力和验收，不能在 UI 中假定已经实现。
物理网卡与不同物理路径的实际故障验证继续等待硬件条件。
