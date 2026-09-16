# 2026-09-17 重测后的路线决策

状态：已根据 2026-09-16/17 控制连接复用重测结果，作为下一阶段开发入口。

证据包：`D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results`  
实验 commit：`16b377359494f19f386ba5d375d353449e45f7a0`  
实验参数：`control_reuse=worker`，深圳/上海双向，真实 GridFTP GSI 对照。

## 决策

1. `worker` control reuse 保留为目录传输默认策略。它没有改变单文件数据面瓶颈，但目录传输的控制连接数已经降低，tree hash 全部正确。
2. 暂停 hot-path compression 和 scheduler 参数优化。global scheduler 上传仍产生 compression work item，即使 case 配置为 compression off，当前数据不能支持 scheduler 策略结论。
3. 下一阶段先修实验事实模型和资源治理，再做小规模、可复现的目录数据面 profiling。
4. io_uring 与 resume 单独验收，不与大矩阵混跑；本轮没有形成它们的 go/no-go 结论。

## 关键事实

- 单文件 256 MiB：CPNetFlux 77.14/78.79 Mbps，GridFTP 87.35/85.42 Mbps，差距约 8–12%。
- dense 目录：CPNetFlux 42.40/29.75 Mbps，GridFTP 79.78/51.03 Mbps，差距约 42–47%。
- mixed 目录：CPNetFlux 40.66/32.62 Mbps，GridFTP 79.27/48.40 Mbps，差距约 33–49%。
- 68 个 `fail_correctness` case 的 tree/file hash 全部相同；它们是 wire accounting 或 manifest evidence 审计问题，不能直接称为数据错误。
- 18 个 io_uring case 因缺少真实 liburing 被 blocked；6 个 runtime failure 与实验目录耗尽空间有关。

## 阶段 0 验收

- [ ] 将 transfer status、integrity status、evidence status、wire accounting 分列。
- [ ] `compression=off` 时禁止 scheduler 创建 compression work item，并加入参数传播测试。
- [ ] 每个 case 完成后清理可再生 payload；运行前检查磁盘预算；失败时保留最小诊断包。
- [ ] 正式实验只接受固定 commit/归档构建，记录工作树状态和二进制 hash。
- [ ] 用代表性小矩阵重测：单文件 1/8 connections、dense/mixed 目录 1/4/8 file parallelism、scheduler 三策略各 3 次。

## 阶段 1 之后的顺序

1. 目录数据面阶段计时：控制命令、数据连接建立、首块等待、有效读写、manifest/finalize。
2. compression 完全隔离后，再比较 scheduler off/fixed/adaptive。
3. 有真实 liburing 和磁盘预算的环境中单独做 IO；随后做小目录 resume 故障注入。

## 协作规则

- 本地架构任务维护决策、路线图和验收标准；云端任务只运行指定 commit，不在正式源码树开发。
- 每个任务必须写明输入 commit、非目标、命令、输出目录、验收条件和失败分类。
- 没有 raw case、环境信息、命令审计和完整性证据，不把性能数字写入路线图。
