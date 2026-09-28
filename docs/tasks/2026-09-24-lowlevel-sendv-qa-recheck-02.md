# LOWLEVEL-SENDV-QA-RECHECK-02：独立复核 vectored write 证据

- 路线/任务版本：`R2026-09-24.9 / v1`
- ID：`LOWLEVEL-SENDV-QA-RECHECK-02`
- 责任：04 测试与质量；验收：00 总指挥
- 状态：ready

## 目标与范围

重新独立审查既有 `LOWLEVEL-SENDV-IMPL-01` 候选实现及其 Linux A/B 原始证据。上次 QA 因无法读取 WSL ext4 原始文件而给出 PARTIAL；当前总控已将仅供审计的 26 MiB 子集导出到 Windows 外部证据目录。此任务用于决定 sendv 是否可作为单文件低层优化保留/进入匹配环境验证，不把本机 loopback 结果当作 100 Mbps 收益。

非目标：不改代码、测试、BOARD/ROSTER、旧回执或历史 payload；不重新跑性能 A/B、不访问云端/SSH、不做传输实验、不改工作树或 index、不提交。唯一工作区产物为本任务结果与 CLI last-message。

## 固定输入及证据

- 源码输入提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- 实现 worktree：`C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-sendv-impl-01\worktree`；分支 `codex/LOWLEVEL-SENDV-IMPL-01`。先核实 HEAD、status、index 和代码路径白名单。
- A/B 审计子集：`D:\Project\CPNetFlux-evidence\LOWLEVEL-SENDV-QA-RECHECK-02`。其 `SHA256SUMS.audit-bundle` SHA-256 为 `b43a129ba2709a80313b65438c0c6c61415e50fed2ca4e682f27c07714fdd8ce`；先运行 `sha256sum -c` 或等价校验。该子集来自只读源目录 `/home/sumu/cpnetflux-evidence/LOWLEVEL-SENDV-IMPL-01-root-final-20260923`，没有包含其中约 871 MiB 的 tree-stage 资料或任何 payload。
- 关键预期指纹：source archive `d541882f34603aab82a97078cb1e821cb8030df0cffeb5692cecb099835282e9`；新客户端 `b818ea8f489f76533c55dba55dc4d9e463c6a2a00260c513e087a7ce1b65c830`；基线客户端 `4be0978b01b16c9d21dc6f47d3d73e1755f9b3ea3dee625fdd1af57d4aa04e21`；A/B 汇总 `summary.json` `88c8c612fb7d2ab13554f14c8a866a51220bcc76f1bf7c587969210c98e24167`；逐行 CSV `results-corrected.csv` `b45256a8921877bcd4f01bcc6d88b4745c6de486684fa102a653dfca7a50d19a`。
- 上次 03 实现回执：`docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md`；上次 04 复核：`docs/tasks/2026-09-23-lowlevel-sendv-qa-01-result.md`。

## 验收要求

1. 对 bundle 校验清单及上述关键哈希；核对归档、二进制和测试日志与回执所述对象一致。若只验证了 bundle 内部一致性、无法与源 WSL 文件独立比对，明确注明证据边界。
2. 从原始 `strace` 对 DATA peer 的记录独立复算 1/8 connections 的 before/after DATA frame 数、`sendto` / `sendmsg` 形态和写调用数；与 CSV/JSON 汇总交叉核对。不要把所有进程调用总数误作 DATA peer 调用数。
3. 核实六个未跟踪 wall case 的输入、客户端/服务端退出状态及源/目标 SHA-256；核实 CPU/GiB 算法。指出被 `strace` 跟踪的 case 不能用于无观测器 wall 比较。
4. 重新计算 1/8 connections 的 wall 与 system CPU 中位数/离散度，并解释小样本局限。尤其审查 8-stream wall `0.23→0.25 s` 的回归是否能被三次样本支持或排除。
5. 静态复核实现的 iovec 推进、短写/EINTR/零写/peer close、TLS/空 payload fallback、两个 DATA 发送 callsite 和专属单测。区分代码正确性、syscall/CPU 机制、wall/端到端收益三种结论。
6. 给出 `PASS`、`PARTIAL` 或 `FAIL` 的独立结论及明确取舍：能否保留未合入原型；是否还需更大本机 A/B；在深圳—上海匹配认证/data TLS 复验前，不批准跨域收益结论。

允许只读检查实现 worktree 和指定外部证据。不得改动其它文件。构建/CTest/新 A/B 本任务均不要求；若自行发现有必要的验证，不扩大范围，列为下一任务。

## 输出及结束条件

- 结果文件：`docs/tasks/2026-09-24-lowlevel-sendv-qa-recheck-02-result.md`
- CLI 摘要：`docs/tasks/2026-09-24-lowlevel-sendv-qa-recheck-02-last-message.md`
- 写后执行 UTF-8/尾随空白/路径门禁、`git diff --check`、输入提交、暂存区与实现 worktree 状态复核。
- 结论不能把 syscall 次数下降自动记为 wall 提升；不得因输出空间已释放或 SSH 可用而启动云端验证。
