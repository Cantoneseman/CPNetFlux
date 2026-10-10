# V2-COMPLETE-01 阶段结果

日期：2026-10-10
分支：`codex/V2-COMPLETE-01`
输入提交：`5e9922c6f430ab0a36f8d7b001fca32f7d9ecea6`

## 已完成

- V2 persistent tree 默认开启；V1/旧 tree 路径只在显式 `--data-session-reuse off` 时使用。
- FILE_BEGIN 传递 checksum algorithm，FILE_END 传递并校验 CRC32C digest；身份、offset、generation、重复提交和异常 payload 均拒绝。
- V2 resume 复用已有 TransferSession/DownloadSession manifest；已完成文件跳过，下载端按 relative path 复用稳定 transfer_id，动态队列保持原始 file_id。
- V2 控制协商显式核对 checksum、resume、data TLS 和动态调度响应；不兼容能力直接返回错误，不静默降级。
- 数据通道接入 TLS required，保留原有控制通道 TLS 配置；增加 checksum 阶段计时。
- smoke 门禁中所有旧 tree 测试显式选择 V1，避免默认语义与旧测试相互污染。

## 验收证据

- 定向 CTest：`PersistentSessionTest` 15/15；动态队列、目录 batch、manifest、tree options 全部通过。
- 全量 CTest（排除需要未提供 `CPNETFLUX_TEST_TOKEN` 或外部 soak 资源的三项）：`258/258` 通过；io_uring 可选测试按环境跳过。
- V2 checksum=crc32c 动态 mixed：57 文件、24,315,364 bytes，上传/下载/resume tree hash 一致。
- V2 checksum=crc32c dense：128 x 1 MiB，上传/下载 tree hash 一致。
- data-channel TLS tree smoke：上传/下载均通过，证书由 smoke 临时生成。

## 尚未完成

- V2 大文件 offset range 调度仍由后续独立任务实现；当前大文件按文件级传输。
- resume 当前安全重传缺失内容所在文件，尚未实现跨 range 的只重传协议；协议级 missing-range 已由既有 session 层提供。
- V1 源码删除留待单独审计任务；本阶段只切断默认隐式 fallback。
- token smoke 和 alpha soak 未运行通过：前者缺少环境变量，后者依赖外部资源；未把失败伪装成通过。

## 回滚

回滚到输入提交 `5e9922c6f430ab0a36f8d7b001fca32f7d9ecea6`；本阶段没有修改深圳主工作区，也没有触碰 Windows 副本、上海对端或其他项目。
