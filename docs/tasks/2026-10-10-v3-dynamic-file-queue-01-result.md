# V3-DYNAMIC-FILE-QUEUE-01 实现结果

## 输入与版本

- 权威仓库：`/root/projects/CPNetFlux`；实现 worktree：`/tmp/cpnetflux-runs/V3-DYNAMIC-FILE-QUEUE-01/src`。
- 输入提交：`de11e15bcc8f92e8d730be1b2483420bd528ef39`。
- 任务范围提交：`3461e09ed7f46e0fcfdb15196ae717c5b3006311`。
- 实现提交：`d0cfb5dbdfb8b2949bd1a0ead3b700c5753f2628`。
- 分支：`codex/V3-DYNAMIC-FILE-QUEUE-01`。
- GitHub：`origin` 为 `git@github.com:Cantoneseman/CPNetFlux.git`；远端回读
  `refs/heads/codex/V3-DYNAMIC-FILE-QUEUE-01` 与实现提交一致。

## 已实现

- V3 动态目录使用有界元数据环状队列，按稳定 LPT 顺序入队；通道以原子 claim/complete 领取，校验 file identity、generation、transfer_id、path、size、mtime 和 chunk_size。
- 动态目录加入批次 ID、全局文件去重、通道参与者门槛、超时、取消、控制断线回收和数据 socket 关闭；静态模式保留原固定 shard 和失败后 drain/继续行为。
- V2 persistent tree 增加动态/静态 scheduling 选择；旧服务不支持动态能力时，在首 payload 前回退 V1，并在 JSON summary 中写出实际模式和原因。
- 增加阶段与逐文件 timing：queue wait、control prepare、data connect、first payload、payload I/O、file result、transfer complete wait、manifest/finalize；未观测字段保持 null。
- 未启用 checksum、resume、TLS、range，也未修改桌面端或把动态模式提升为默认 V2 路径。

## 验收结果

证据目录：`/tmp/cpnetflux-runs/V3-DYNAMIC-FILE-QUEUE-01/verify`。

- 定向 CTest：37/37 通过。
- 全量 CTest：260/260 通过；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因环境无 io_uring 跳过。
- dense 动态双向：hash `08bd922e700c226bc6c344400480e5f665d1bc89c87cced38d216eddbbf6ede8`，128 文件，134217728 bytes。
- mixed 动态/静态双向：hash `932ae4ecd549719bbb9c8e0f936f6544f78dd98990f94eb3d8198e023c3c2ea7`，57 文件，24315364 bytes。
- 大文件完整传输：hash `1112a01800a71e197025678064653fc669ce970bae57b9045571e47411b0c3e7`，1 文件，67108864 bytes；range 仍 deferred。
- 旧固定 V2 服务兼容：旧服务端二进制 SHA256 `8905de3907b5f9aebd6ccbe8840fd31fe95a3deb039d3894fa13f92b35fdbe69`；上传和下载均 `actual_mode=v1`、`actual_file_scheduling=v1`、`fallback_reason=v2_requested_capability_unsupported`，hash `ddbca9a45490788f95d0f3199763f2b0ca889dce4b035525c73043cce4ef235a`，3 文件，65538 bytes。

## 下一步

由 00 总指挥安排短实验前，先继续独立代码阶段：补 checksum/manifest/finalize 语义与 resume 错误恢复的统一验证，再设计大文件 range。当前未启动 TLS、range、桌面端、跨域实验或性能矩阵。
