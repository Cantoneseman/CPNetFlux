# R2026-10-19：未部署项目只保留 V3 运行和当前输入格式

## 背景

CPNetFlux 尚未正式部署，没有需要保护的 V1/V2 用户端、旧服务器或历史生产断点文件。此前代码为了升级过渡保留的旧 manifest 读取分支，会让“运行协议版本”和“文件结构编号”混在一起，也让后续维护者误以为系统仍有 V1/V2 运行路径。

## 决定

- CPNetFlux 的客户端、服务端和传输状态机只有 V3。
- 删除未部署前遗留的 V1 transfer manifest 读取和恢复分支；V3 只读写当前 manifest schema。
- 当前 manifest、download manifest、tree manifest 和 frame 的数字编号是序列化/帧结构编号，不是运行时 V1/V2；代码常量使用 SchemaVersion 或 FrameSchemaVersion 命名，避免把它们误解为旧运行版本。
- 当前 V3 中断后产生的 partial file、manifest 和 resume 继续支持；这属于 V3 自身的断点能力。
- 旧 V1/V2 迁移文档和实验记录保留为历史证据，不作为可运行能力或验收依据。

## 非目标

本决定不改 wire frame 的数字 schema、不改认证配置、不改生产服务数据，不验证物理网卡/local-bind/不同物理链路，也不把上海到深圳 TCP/2121 的现有网络阻断说成已解决。

## 验收

1. 源码中不再存在 transfer manifest 的 V1 兼容解析或恢复分支。
2. 当前 V3 manifest、download manifest、tree manifest 和 frame 的 schema 常量名称不再使用 V1/V2 运行版本命名。
3. 当前 V3 的新建、保存、加载、resume 和损坏 manifest 拒绝测试通过。
4. 深圳与上海从同一提交构建，二进制 SHA-256 一致，再进行 V3 双向传输验证。
