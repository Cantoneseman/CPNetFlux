# 2026-09-23：单文件 vectored write profile gate

- 路线版本：`R2026-09-23.4`
- 输入实现：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 证据：`docs/tasks/2026-09-23-lowlevel-single-profile-00-result.md`

profile 已在固定提交的 WSL Linux Release 构建中完成：256 MiB plain TCP、buffer 64 KiB、chunk 1 MiB、checksum none，connections 1/8 各 3 次；每次有 4,096 个 DATA frame，逐 peer trace 观测到 4,096 个 64-byte header send 和 4,096 个 payload send，0 short/unresolved，目标 hash 与源一致，file transfer/resume/checksum smoke 3/3 通过。

因此满足设计审查要求的机制门，授权独立 `LOWLEVEL-SENDV-IMPL-01`。授权只覆盖 plain TCP DATA header+payload vectored write 及必要测试；TLS、协议格式、checksum/resume、目录 lookahead、manifest batching、默认并行度和云端实验仍需各自门禁。实现后的收益必须通过同输入 before/after A/B 复核；syscall 下降而 CPU/GiB 或 wall 无实质收益时保持回退，不把局部机制结果称为深圳—上海验收。

