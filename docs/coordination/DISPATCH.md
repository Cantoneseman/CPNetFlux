# 总指挥派单操作

六个长期聊天已登记在 ROSTER.md。当前本机 Codex CLI 能使用与桌面相同的本地会话存储。优先使用当前工具中提供的既有聊天派单能力；缺少专用工具时，可使用下面的 CLI 续接命令。不要重复创建角色。

## 向既有角色派单

先确认角色当前没有正在执行的轮次，也没有被另一个控制进程持有写入会话，并把有版本的任务单写入 `docs/tasks/`。只派发用户已授权且依赖满足的范围。命令示例（PowerShell，cwd 为 `D:\Project\CPNetFlux`）：

```powershell
Get-Content -Raw -Encoding utf8 'docs/tasks/<实际任务文件>.md' | codex exec resume <ROSTER中的真实聊天ID> -
```

`codex exec resume` 是已安装 CLI 帮助中公开支持的入口，使用既有会话 ID 持续同一聊天；初始 prompt 已在聊天内，后续只发任务与更新，不重新粘贴全部历史。不要使用 `--last`，以免续接错误角色；不要传 `--ephemeral`。

命令可能持续运行。总指挥用执行工具返回的 session ID 跟踪同一进程；超时但进程仍活跃不等于失败，不重复启动相同任务。命令结束后检查退出状态、角色回执和实际产物，不能只凭进程退出码判断验收通过。

角色只写自己的回执或任务单授权文件。共享开发目录不要多角色并发修改同一文件或 Git index。通常同时最多运行三个有独立文件范围的专职任务；有环境占用的实验串行。

## 会话协议入口

本次创建通过 `codex app-server --stdio` 的 JSON-RPC 完成；使用的协议 schema 由 `codex app-server generate-json-schema --experimental --out <临时目录>` 生成，不猜测内部 API。调用顺序为 initialize（experimentalApi=true）、initialized，然后 project/create、thread/start、thread/name/set、turn/start。读取通过 project/read、thread/read、thread/list（可选 useStateDbOnly=true）。

`thread/start` 的 projectId 与 cwd 都必须正确；仅设置聊天标题不等于项目归属正确。缺少专用工具时，需读当前安装版本生成的 schema 再使用协议，不能修改私有数据库或伪造 rollout。

## 运行时限制

- `codex queue` 依赖共享 daemon；本次 daemon control socket 连接失败，不能假定 queue 可用。
- 独立 CLI 若报 `already has an active writer`，不能强行重试、删锁或杀别的用户会话。优先由持有该会话的同一 app-server 用 turn/start/turn/steer 调度；原控制进程在全部轮次结束后正常退出，才改用独立 CLI。`thread/unsubscribe` 只取消订阅，本次实测不足以释放 writer。
- 2026-09-17 22:06：设置 app-server 经 stdin EOF 正常退出（code 0）后，独立 CLI 成功续接 `01a0af8d-b203-7400-b998-437278bdb724`，返回同一 thread_id 和 turn.started；证实正常释放控制进程后可继续原会话。
- 默认新会话可能为只读。创建/派单应在用户已授权范围内沿用项目当前访问设置，并处理具体工具请求；不能让角色的写回执任务无期限等待无人处理的确认。不能把有限核验扩展为云端清理或新实验。
- 桌面锁屏时不继续 UI 输入；CLI/API 能力与桌面可见状态分别报告。
- 若某一轮模型声称没有文件工具，先让它核查实际暴露工具、进行最小 `git status`/文件读取；其他角色已实际读取工作区，不能直接断言整台机器不支持。
- 若出现真实服务错误，记录当前会话/轮次，重查终态后再续接；不要新建一组聊天绕过。
- 本次限制：CLI/API 从设置线程向角色发消息已验证；00 自身后续没有 shell 工具，00→04 检查未发出。临时 code_mode 开关无效，未修改全局配置。不把通用 CLI 支持等同于每个模型轮次都有调用能力。
