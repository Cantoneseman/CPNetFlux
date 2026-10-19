# V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01

Date: 2026-10-19 (Asia/Shanghai)
Status: complete
Owner: current commander, single serial developer
Input commit: 30aaf5d10f70244ea35d911711cc001bf21ba53e
Branch: codex/V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01
Scope commit: d9b022900d1ac163f448921246891a99fc035d2d
Implementation commit: e545e4b1bd328e6615d5b0bdd0bca1fedd0092db
Final documentation commit: 669d3f0eda75e3aa0917781db580373d784e347b

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

## 实际完成

- 删除 transfer manifest 版本 1 的解析器和 `TransferSession::resume` 兼容分支；传入 `manifest_version=1` 现在明确失败，不会再切到旧执行路径。
- 将当前文件 manifest、download manifest、tree manifest 和 frame 的常量改成 `*SchemaVersion` 命名，避免把文件结构编号误读成 V1/V2 运行版本。
- 更新单元测试、README、当前 V3 决策和任务板；V3 只读写自己的当前 manifest/checkpoint，历史 V1/V2 不再是运行或验收对象。
- 深圳和上海从同一源码归档构建，随后把同一 server 二进制安装到两端正式服务并重启；两端运行二进制 SHA-256 相同。

## 验收证据

- 源码归档：`/tmp/cpnetflux-runs/V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01/source-e545e4b.tar.gz`，SHA-256 `6cbbad8ed3b19ce0774ddd9d802509c3aa2c91ba141b0a494824cbcb0c0d641c`。
- 深圳和上海均使用 Release、TLS ON、io_uring ON、desktop OFF、tests ON、cross-domain OFF 配置；最终 CTest 均为 `393/393` 通过、`0` 失败。
- 两端构建出的 server、tree upload/download client、file server/client/download client 的 SHA-256 完全一致；server 为 `e952ea928fd6c7753df6cbe4b558e829232df40b481d0f36cbbe4a860827f412`。
- 两端 `cpnetflux-demo.service` 均 active，运行 server SHA-256 为上述新值；升级前两端 server SHA-256 均为 `d40a3932dffc24f2eff9f82020e3c8ba133a9d7ed7f67953b039000f3c1fea6e`，旧文件保留在任务临时备份目录。
- 深圳客户端到上海正式服务：上传和下载均通过，3 个文件、2,621,488 字节，`actual_transport=v3`、`actual_mode=persistent_tree`、`fallback_reason` 为空，树哈希 `crc32c:d1784ace`，源/目的 SHA-256 一致。
- 上海客户端到深圳临时回环 V3 服务：通过 SSH 隧道完成双向上传/下载，3 个文件、1,782,617 字节，`actual_transport=v3`、`actual_mode=persistent_tree`、`fallback_reason` 为空，树哈希 `crc32c:a0039228`，源/目的 SHA-256 一致。
- 反向测试的临时服务、隧道、任务 payload、上海正式服务测试目录和本地临时认证文件均已清理；摘要和日志仅作为证据保留。

## 仍然存在的边界

- 上海主动直连深圳 TCP/2121 仍受网络策略阻断；反向方向使用了 SSH 隧道到深圳回环临时服务，因此不能表述为上海直连深圳生产端口已通过。
- 物理网卡、local-bind、不同物理链路和吞吐验证按用户要求后置，不影响本轮 V3-only 代码和软件层双端互传验收。

## 下一步

继续 V3-only 组合稳定性和故障恢复路线：把已验证的 lane、控制连接和重试预算组合进稳定候选矩阵，并在每次改动后同步两端源码、完成双端构建后再互传验证。V1/V2 运行逻辑、旧执行器和历史 checkpoint 互操作不再作为开发输入。
