# V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01

Date: 2026-10-19 (Asia/Shanghai)
Status: in_progress
Owner: current commander, single serial developer
Input commit: 30aaf5d10f70244ea35d911711cc001bf21ba53e
Branch: codex/V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01
Scope commit: pending

## 实际问题

项目还没有正式部署，但源码仍把旧 transfer manifest 的版本 1 当成可恢复输入，并在 V3 session 中为它保留专门分支。这样会让人误以为系统需要同时维护 V1、V2、V3 三套能力，也不符合“第一次正式使用就是 V3”的产品边界。

## 本轮范围

- 删除 transfer manifest 的旧版本 1 解析和 TransferSession::resume 兼容分支。
- 把当前文件 manifest、download manifest、tree manifest 和 frame 的 schema 常量改成不带 V1/V2 运行含义的名称。
- 更新受影响的测试、注释、README 和当前 V3 决策，明确 V3 自己产生的断点文件仍可恢复。
- 在深圳构建同一提交；同步源码到上海后用同一配置构建并核对二进制。
- 构建完成后，用 V3 客户端/服务端完成跨机上传和下载，确认最终文件哈希一致。

## 非目标

- 不修改历史决策和实验报告中的事实记录。
- 不重做物理网卡、local-bind 或不同物理链路验证。
- 不修改当前 wire frame 的数字 schema，也不改认证、服务配置或生产数据。
- 不把上海到深圳 TCP/2121 的现有网络限制描述为已解决。

## 验收标准

1. manifest_version=1 的 transfer manifest 被明确拒绝；当前 V3 manifest 新建、保存、加载、resume 和损坏校验继续通过。
2. 源码 active path 不再出现 transfer manifest V1 兼容常量、解析分支或“legacy checkpoint”运行描述。
3. 深圳和上海从同一提交完成 Release/TLS/io_uring/CTest 构建，服务端和两个客户端 SHA-256 完全一致。
4. 跨机 V3 上传/下载通过，summary 明确 actual_transport=v3、无 fallback，源/目的 SHA-256 一致。

## 下一步

完成本轮清理后，继续 V3 组合稳定性和故障恢复路线；历史 V1/V2 不再作为开发输入或验收条件。
