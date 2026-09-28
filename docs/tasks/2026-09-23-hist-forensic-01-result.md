# HIST-FORENSIC-01：旧跨域性能证据取证结果

- 原路线/任务版本：`R2026-09-23.3 / v2`；本次恢复任务：`R2026-09-23.4 / v1`。
- 执行角色：02；结果状态：恢复完整正文，等待 00 验收。04 的独立复核摘要仍单独保留。
- 本地输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。本轮开始及结束 HEAD 相同。
- 旧运行身份：`16b377359494f19f386ba5d375d353449e45f7a0 + dirty`。旧环境记录本地工作树状态有 78 条非空状态行；旧报告也记载 78 项未提交修改。因此旧结果不是仅凭 commit 可重建的固定构建证据。
- 原始证据根：`D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full`；`results/` 与同级 `RETEST_REPORT_ZH.md` 在上一轮取证时均存在。
- **恢复说明：** 当前正文恢复自上一轮 02 已完成的分层核验结果和同一聊天中保留的分析记录；本轮没有重新扫描完整 raw 集、重算 payload 或重跑旧分析。上一轮报告记录的输入文件指纹和下列派生值一并恢复。04 的 `hist-qa-01-result.md` 当前是简短审查摘要，只作为独立确认 58/14/38 计数和解释边界的依据，不将它表述为完整 QA 报告。

## 结论

旧矩阵可支持有边界的历史诊断，不能建立同安全配置下 CPNetFlux 与真实 GridFTP 的正式性能差距。GridFTP 实际由外部 `globus-url-copy` 执行，环境记录 GSI，命令包含 `-dcpriv`；CPNetFlux 样例服务端记录 `anonymous`、控制 TLS off、数据 TLS off。按完整配置匹配边界，严格 `performance_eligible` 样本数为 **0**。

可以保留两种描述性数字，但均不是完整匹配性能结果：

1. 从 `results.csv.elapsed_seconds` 重算出 **58 个 runner-case-wall 配对**。这些行按数据内容 hash、方向、工作负载并发、退出状态及实测流数筛选；另有 14 个 pair 因流数不一致排除。wall 包含 runner 准备/收尾，不能当纯传输吞吐。
2. 从 `command.jsonl.duration_seconds` 得到 **38 个目录客户端进程配对**（dense 23，mixed 15）。两侧命令窗是客户端程序从启动到退出，包含各自的目录扫描/规划、控制、数据和结束等待；独立 hash 在客户端命令之外。这是边界相对清楚的客户端端到端诊断，但仍有 GSI/TLS 不等价，故严格 eligible 仍为 0。

单文件客户端命令时间不纳入配对 goodput：CPNetFlux 文件客户端在 runner 已发出 `STOR`/`RETR` 后才启动，并在退出后由 runner 读取最终控制回复；GridFTP CLI 自行包住控制和数据过程，两个命令边界不一致。CSV wall 也不能代替纯传输计时。本证据无法恢复 socket payload-only duration。

## 原始审计、输入及 workload

上一轮读取的 `case_plan.json` 有 218 个唯一 `case_id`；`results.csv` 有 216 行、216 个唯一 `case_id`，无重复 ID、无计划外行。差集是 GridFTP mixed-tree resume 双向两个计划 case。`summary.csv` 有 72 行、72 个唯一聚合键；按 raw result 逐组汇总的 repeat/pass/blocked/fail 数与之相符。

| 历史证据文件 | 字节数 | 上一轮记录的 SHA-256 |
|---|---:|---|
| `results/results.csv` | 1,433,765 | `F84888CC899CF291A6B65E088958D3B201AF462998E8BEFC9DFE64C33AE58E86` |
| `results/summary.csv` | 8,758 | `CBD6368A19086B245CCA660F1F43148A99650ECF310C9BCD04121762C4C66AFF` |
| `results/case_plan.json` | 135,851 | `DE351FC1B8EB506378D454DD93C9B55D59A2F73E259F2BCD67B38CAF7A5CFD42` |
| `results/dataset_manifest.json` | 73,154 | `8B50CB6147B5F8D218C0238F77602523C0F512B05D51A5B9CB10D0140190F0F6` |
| `results/environment.json` | 5,309 | `093129CCDD849780B4AA82876395232D0493A1545AE1FE4DAE209EB79EF1B0C8` |
| `results/command.jsonl` | 2,279,707 | `F18EE6C484BAB72C5F652B85A455EA4C609F879AD197651E46B561EF03D71C6E` |
| `RETEST_REPORT_ZH.md` | 7,771 | `10EF28469D6385A891A2DC9E44BA1CE1057919506BD78F19E0A3D43192652039` |

`dataset_manifest.json` 记载 seed `20260831`、generator `gridftp_compare_dataset_v1`、`materialize_catalog=false`；catalog 的 single SHA 字段为空，因此输入身份从各 case 的运行时 hash 记录核对。所选 fresh core workload 均为 `resume=false`、`checksum=none`、`compression=off`、CPNetFlux `scheduler=off`、`control_reuse=worker`、POSIX I/O。

| workload | 精确逻辑字节 | 文件数 | 规模标签 |
|---|---:|---:|---|
| `single_256MiB` | 268,435,456 | 1 | single |
| `tree_dense_128MiB` | 134,217,728 | 128 | dense |
| `tree_mixed_256MiB` | 268,435,456 | 148 | mixed |

方向 `local_to_remote` 是深圳到上海，反向是上海到深圳。`repeat_index` 范围按原 plan 保留；不能视作随机化时间 block。

## Hash 口径及限制

`hash_match` 是旧 `results.csv` 中的字段，由旧 runner 的源/目的实际内容 hash 比较产生；本轮没有重读 payload。命令审计中有 `remote_sha256` 或 `remote_tree_hash` 阶段。目录远端 hash 脚本逐文件读取并计算 SHA-256，再将相对路径、字节数和文件 SHA 纳入 tree hash。所需 payload 未保留，故本文报告的是已有 raw 运行时 hash 记录的核对，不是本轮独立 payload 重算。

single 两系统 CSV hash 表示不同：CPNetFlux 字段是文件 SHA-256，GridFTP 字段是单文件 tree hash。上一轮按以下 canonical 算法规范化 CPNetFlux 字段：

```text
tree_hash = SHA256(UTF8(relative_path) || 0x00 || ASCII(size) || 0x00 || ASCII(file_sha256) || 0x00)
```

以 `relative_path=single.bin`、`size=268435456`，CPNetFlux file SHA `8d481d02b7c08e9174b74e8599ed4ba6fb61586bd2b3d99acdec667e805b8dc5` 规范化为 `14568f6377b15b8b4efb408f33f72dd8b3e58e0b796aac399b8535c8b710c377`，与 GridFTP 记录的源/目的 tree hash 一致。24 个 single 跨系统配置/方向/repeat 对都通过此转换。dense/mixed 的 48 个跨系统配置/repeat 对中，source 与 destination tree hash 逐字相同。该步骤只规范化 CSV hash 表示，不能宣称重新验证了归档 payload。

## 配对定义、计时与排除

所有派生 goodput 均为十进制 Mbps：`logical_bytes * 8 / duration_seconds / 1,000,000`。匹配键：

```text
(dataset, direction, file_parallelism, per_file_connections, repeat_index)
```

每个候选 pair 要求 CPNetFlux 与 GridFTP 都有记录，输入 hash 规范化后相同，各自 `exit_code=0`、`hash_match=true`，并且 `max_observed_data_streams` 相同。配对比为同格同 `repeat_index` 的 `CPNetFlux Mbps / GridFTP Mbps`；报告中位数和 range/median，其中 range/median 定义为 `(max(pair ratio)-min(pair ratio))/median(pair ratio)`。每格 n 只有 2–3，属于描述性离散度，不是统计显著性检验。

`repeat_index` 只用作匹配标签。plan 按系统把同格 CPNetFlux repeats 连续排完，再运行 GridFTP repeats；不是随机交错的 block。因此不能排除链路随时间变化造成的混杂。

初筛有 72 个 workload/config/repeat 配置候选（single 24、dense 24、mixed 24）。14 个配置因实测流数不同而排除，剩 **58 个 runner-wall diagnostic pairs**：

| 排除格 | 排除数 | 原因 |
|---|---:|---|
| single 下载，所有 4 个连接档的 r0 | 4 | `max_observed_data_streams` 不一致 |
| dense 下载，fp1/c1 r0 | 1 | 实测流数不一致 |
| mixed 下载，c2 下 fp1、fp2、fp4 各 3 repeats | 9 | 实测流数不一致 |
| **合计** | **14** | 不计入配对分母 |

其余 58 对可作内容/工作负载/实测流数相符的历史诊断，不是安全配置完整相配结果。两侧 GSI/数据保护与 anonymous/TLS off 不等，严格 performance-eligible 分母为 **0**。

## Runner-case-wall 配对诊断

下表由 CSV 原始 `logical_bytes` 和 `elapsed_seconds` 重算；不使用 `logical_goodput_mbps`，因为旧 classifier 会让一些 tree 行的该字段为空。某些 CPNetFlux tree 行仍标记 `fail_correctness`，本表只以原始 exit/hash/elapsed 建立诊断量，并保留其历史失败分类。表中 wall goodput 与 ratio 均不是纯 transfer 指标。

| 数据集 | 方向 | 并发（文件并行×每文件连接） | CPNetFlux wall 中位 Mbps | GridFTP wall 中位 Mbps | 配对比值中位数 | 比值 range/median | n |
|---|---|---:|---:|---:|---:|---:|---:|
| single 256 MiB | 深→沪 | 1×1 | 79.85 | 88.60 | 90.6% | 5.1% | 3 |
| single 256 MiB | 深→沪 | 1×2 | 74.45 | 86.87 | 85.6% | 2.7% | 3 |
| single 256 MiB | 深→沪 | 1×4 | 78.13 | 87.24 | 89.6% | 8.9% | 3 |
| single 256 MiB | 深→沪 | 1×8 | 76.87 | 87.09 | 88.1% | 5.4% | 3 |
| single 256 MiB | 沪→深 | 1×1 | 80.16 | 89.93 | 89.1% | 1.1% | 2 |
| single 256 MiB | 沪→深 | 1×2 | 76.86 | 84.85 | 90.6% | 1.8% | 2 |
| single 256 MiB | 沪→深 | 1×4 | 80.08 | 87.56 | 91.6% | 8.9% | 2 |
| single 256 MiB | 沪→深 | 1×8 | 79.22 | 81.32 | 97.8% | 11.0% | 2 |
| dense 128 MiB | 深→沪 | 1×1 | 16.70 | 79.68 | 21.0% | 6.4% | 3 |
| dense 128 MiB | 深→沪 | 2×1 | 31.54 | 77.68 | 40.6% | 4.9% | 3 |
| dense 128 MiB | 深→沪 | 4×1 | 53.27 | 78.71 | 67.7% | 2.1% | 3 |
| dense 128 MiB | 深→沪 | 8×1 | 67.99 | 81.54 | 83.4% | 1.4% | 3 |
| dense 128 MiB | 沪→深 | 1×1 | 13.33 | 20.96 | 63.6% | 4.3% | 2 |
| dense 128 MiB | 沪→深 | 2×1 | 22.98 | 39.61 | 58.0% | 4.2% | 3 |
| dense 128 MiB | 沪→深 | 4×1 | 35.41 | 69.34 | 50.8% | 14.0% | 3 |
| dense 128 MiB | 沪→深 | 8×1 | 46.89 | 73.33 | 63.9% | 8.1% | 3 |
| mixed 256 MiB | 深→沪 | 1×1 | 28.78 | 76.77 | 37.5% | 4.0% | 3 |
| mixed 256 MiB | 深→沪 | 1×2 | 28.60 | 72.17 | 39.9% | 13.4% | 3 |
| mixed 256 MiB | 深→沪 | 2×2 | 44.88 | 84.67 | 52.1% | 5.5% | 3 |
| mixed 256 MiB | 深→沪 | 4×2 | 61.18 | 82.95 | 75.1% | 6.5% | 3 |
| mixed 256 MiB | 沪→深 | 1×1 | 23.66 | 35.22 | 67.2% | 5.1% | 3 |
| mixed 256 MiB | 沪→深 | 1×2 | — | — | — | — | 0/3：实测流数不匹配 |
| mixed 256 MiB | 沪→深 | 2×2 | — | — | — | — | 0/3：实测流数不匹配 |
| mixed 256 MiB | 沪→深 | 4×2 | — | — | — | — | 0/3：实测流数不匹配 |

方向顺序：深→沪为 `local_to_remote`，沪→深为 `remote_to_local`。没有跨配置汇总均值；特别是 directory 并行度从 1 到 8 的差异不可折叠成一个“目录平均差距”。

## Directory 客户端进程时长

下表仅用 `command.jsonl` 中 `gridflux_tree_transfer` 与 `gridftp_transfer` 的进程 `duration_seconds`，不使用 CSV wall。客户端进程窗都从相应可执行程序启动至退出，位于数据准备之后；独立 hash 在命令结束后执行。窗口包括客户端自己的目录扫描/规划、控制交互、传输和结束处理，所以是条件性的 client-process goodput，不是 payload socket-only time。仍因认证及数据隐私配置不同而不符合严格性能匹配。

| 数据集 | 方向 | 并发（文件并行×每文件连接） | CPNetFlux client 中位 Mbps | GridFTP client 中位 Mbps | 配对比值中位数 | 比值 range/median | n |
|---|---|---:|---:|---:|---:|---:|---:|
| dense 128 MiB | 深→沪 | 1×1 | 18.11 | 93.52 | 19.4% | 8.2% | 3 |
| dense 128 MiB | 深→沪 | 2×1 | 36.82 | 89.98 | 40.9% | 4.8% | 3 |
| dense 128 MiB | 深→沪 | 4×1 | 70.00 | 91.52 | 76.5% | 4.2% | 3 |
| dense 128 MiB | 深→沪 | 8×1 | 101.43 | 95.17 | 105.6% | 1.9% | 3 |
| dense 128 MiB | 沪→深 | 1×1 | 14.13 | 21.53 | 65.6% | 4.4% | 2 |
| dense 128 MiB | 沪→深 | 2×1 | 25.31 | 42.10 | 60.0% | 4.1% | 3 |
| dense 128 MiB | 沪→深 | 4×1 | 41.85 | 76.22 | 54.5% | 14.5% | 3 |
| dense 128 MiB | 沪→深 | 8×1 | 57.75 | 80.51 | 71.7% | 9.3% | 3 |
| mixed 256 MiB | 深→沪 | 1×1 | 31.09 | 83.71 | 37.1% | 4.7% | 3 |
| mixed 256 MiB | 深→沪 | 1×2 | 30.83 | 78.20 | 39.4% | 14.3% | 3 |
| mixed 256 MiB | 深→沪 | 2×2 | 50.49 | 92.97 | 53.4% | 6.0% | 3 |
| mixed 256 MiB | 深→沪 | 4×2 | 73.01 | 91.76 | 80.7% | 6.2% | 3 |
| mixed 256 MiB | 沪→深 | 1×1 | 24.99 | 36.24 | 69.0% | 5.5% | 3 |

合计 38 个目录客户端进程 pairs（dense 23、mixed 15）。mixed 沪→深的 1×2、2×2、4×2 配置由于 GridFTP 观察到的流数低于 CPNetFlux 对应观测值，不计算比值。举例，dense 深→沪 fp8 的 runner-wall 配对比为 83.4%，客户端进程配对比为 105.6%；计时边界足以改变相对判断。

真实 GridFTP 命令是外部 `globus-url-copy`，目录上传示例形如 `globus-url-copy -fast -dcpriv -cd -rp -p 2 -r -cc 4 file://... gsiftp://...`。同格 CPNetFlux 示例使用 `--file-parallelism 4 --connections 2 --control-reuse worker --checksum none`。GridFTP 行的 `file_io_backend=POSIX` 是 runner schema 标注，不应解释为 GridFTP 具有与 CPNetFlux 相同可控 POSIX 后端。GridFTP 单文件沪→深多连接行走多个 `gridftp_partial_get_*`，不是 CPNetFlux 单文件多连接的相同执行路径；未纳入 client-process 配对表。

## 代表性计时证据

| 样例 | CSV case wall 秒 | 客户端命令秒 | 边界说明 |
|---|---:|---:|---|
| single CPNetFlux 深→沪 1×1，r1 | 26.894 | 20.997 | 文件客户端不含 runner 先发的 STOR 和客户端退出后的最终控制回复读取 |
| single GridFTP 深→沪 1×1，r1 | 24.062 | 21.797 | CLI 自行负责控制与数据连接，无法与 CP 文件客户端窗对齐 |
| dense CPNetFlux 深→沪 fp4/c1，r1 | 20.156 | 15.390 | tree 客户端含自身目录控制及传输；case wall 还包括 runner 前后步骤 |
| dense GridFTP 深→沪 fp4/c1，r1 | — | 11.432 | case-wall 秒数本次恢复材料未列出；不补猜。命令后还有远端 tree hash 等步骤 |

以上 case id 属于 `core_*_fresh_r1`。命令持续时间来自旧 `command.jsonl`。当前只读 `tools/experiments/gridftp_compare/runner.py` 显示现行实现中 `elapsed_seconds` 的单 case wall 时钟跨越准备、hash、服务端收尾；这只能作为当前代码的边界说明，不能证明运行于 dirty `16b3773` 的旧 runner 内部计时语句与当前版本完全相同。raw 中 CSV elapsed 与 command duration 明确是两种字段。由于最后一行 wall 数值在保留的分析记录中没有明确数值，本报告留空。

## 历史结果分类与污染隔离

全矩阵原始分类是 124 `pass`、68 `fail_correctness`、18 `blocked_io_uring`、6 `fail_runtime`；另有 2 个计划 case 无结果。它们是旧标签，不是新 validator 结论。

| 旧分类/差集 | 数量 | 上一轮核验所得解释 |
|---|---:|---|
| `pass` | 124 | 保留历史标签；不构成固定 commit 性能基线。 |
| `fail_correctness` | 68 | 68/68 的 CSV `hash_match=true`、exit 0。39 行 error 同时记录 wire 字段不等和 `verified_chunks` 缺失；29 行只记录 wire 字段不等。不得称为 payload 数据损坏，也不能追改成新 validator pass。 |
| `blocked_io_uring` | 18 | 错误记载 real io_uring backend 未被 `ldd` 检出；是后端前置条件未满足，不是可比较吞吐失败。 |
| `fail_runtime` | 6 | 4 个 POSIX 1 GiB 下载和 2 个 CPNetFlux resume case 有 ENOSPC/`No space left on device` 证据；属于资源阻塞。 |
| 计划无结果 | 2 | GridFTP mixed-tree resume 双向 case；缺失/跳过不等于通过。 |

本报告选入 wall 诊断的 dense/mixed CPNetFlux core 共 48 行，旧 CSV 仍将其标为 `fail_correctness`，但每行 exit 0、source/destination tree hash 相同、elapsed 非空。旧 schema 没有把 transfer、integrity、evidence、wire accounting 分列；tree 行 `logical_goodput_mbps` 为空。因此这里只从原始 logical bytes/elapsed 重算 wall 诊断，并保留旧失败标签，不用旧 summary 的 pass-only goodput。

### Scheduler 的 compression-off 污染

旧 plan/results requested 参数写 `compression=off`。但是 scheduler `global/fixed` 与 `global/adaptive` 上传样例的 `scheduler_metrics/scheduler_summary.csv` 记录每次 `compression_attempts=4096`、`compressed_workitems=4096`。上一轮抽查的样例 logical bytes 为 268,435,456、wire bytes 为 4,391,958、effective ratio 0.0163613。共有 10 个 global fixed/adaptive 上传 case（两策略各 5 次）记录同类 4,096 次压缩 work item，与请求 off 冲突；这些 scheduler 上传必须排除于策略性能比较。scheduler 下载记录 0 次压缩，但整个 scheduler 组均不并入 core 对照，也不由本记录给 scheduler 下结论。

### IO、空间与 resume

18 个 io_uring case 卡在真实 backend 检测。6 个 ENOSPC case 中 4 个是 POSIX 下载、2 个是 CPNetFlux resume。另有 2 个 GridFTP resume 计划 case 没有结果行。现有证据不能形成 io_uring 或 resume 的可靠专项结论，不能与 fresh core 结果混合。

## 候选瓶颈与证据类型

1. **直接观察：dense 上传随文件并行度有强烈变化。** 同为 128 MiB、深→沪、连接/校验/输入 hash 相符的 tree client-process 观测中，CPNetFlux fp1/2/4/8 中位 goodput 分别为 18.11、36.82、70.00、101.43 Mbps；GridFTP 对应约 89.98–95.17 Mbps；配对比从 19.4% 到 105.6%。这是“低文件并行下供给/扩展不足”的候选线索，不证明 scheduler、连接启动、磁盘或 syscall 是原因；且安全配置不等价。
2. **直接观察：mixed 深→沪随并行增加而改善，仍有剩余差距及重复波动。** client-process 配对比从 fp1/c1 的 37.1%、fp1/c2 的 39.4%、fp2/c2 的 53.4% 到 fp4/c2 的 80.7%；最大重复 range/median 14.3%。沪→深只有 1×1 有 3 个流数匹配样本，其他 requested flow shape 被排除。可作为 file-level 并行供给路径的候选问题，需阶段时间验证，不能外推配置。
3. **直接观察：计时边界能改变比较结果。** dense 深→沪 fp8 的 wall ratio 83.4%，client-process ratio 105.6%；single 客户端时间边界还不对称。需冻结双方共同事件边界，独立 hash 与 payload preparation 应在时间窗外。
4. **直接观察：worker control reuse 有运行时迹象，阶段归因缺失。** dense 128 文件和 mixed 148 文件样例的 `client_summary.json`/stdout 记录 `control_connect_count=4`、`control_reconnect_count=0`，分别有 128/148 个 data transfer；事件日志含逐文件 start/complete。现有事件缺少可与 GridFTP 对齐的 scan、control、data-connect、first-byte、payload、finalize 分段时间。
5. **路线文档记载 + 待验证假设：先测目录阶段，再决定底层实现题。** 2026-09-17 `directory-data-plane-profiling` 决策要求记录 `scan_plan`、`control_acquire/prepare`、`data_connect`、`first_payload`、`payload_io`、完成等待、`manifest_finalize`，并警示并行阶段总和不是 wall time。raw 结果不能判定 syscall、manifest rewrite、网络、校验或磁盘中哪项造成差距；这些均为待验证假设。

## 限制、云端准入与复算口径

- 58 个 wall pair 和 38 个 directory client-process pair 是基于旧 seed/hash/流数/退出记录的**诊断分母**，不是同安全级别的正式性能分母；严格 eligible 为 0。
- repeats 每格 n=2–3，顺序未随机交错。range/median 只描述离散度，不作显著性结论；跨配置平均会掩盖并行度差异，因此未使用。
- 历史实例为深圳/上海旧主机和 dirty 构建，不外推到固定 commit、100 G、生产 readiness。GridFTP 的 `-dcpriv` 与 CPNetFlux 的 data TLS off 语义不等价。
- 这次恢复没有新实验授权，也没有资源核实。**该报告不能作为云端开跑准入**：未来批次需要固定提交身份、新冻结的安全/校验/计时契约、两端实时资源和挂载点余量核实、每个挂载点至少 10 GiB 且覆盖 payload/build/evidence 的隔离预算、单时段单一获准批次，以及逐 case 证据先持久化再清理。旧记录中的上海空间阻塞不可推定为已恢复。
- 上一轮可复算规则：只读 `results.csv`、`summary.csv`、`case_plan.json`、`dataset_manifest.json`、`environment.json`、`command.jsonl` 和代表性 case 证据；用 plan/result ID 差集核对缺失；Mbps 用 `bytes*8/seconds/1e6`；wall 配对取 CSV `elapsed_seconds`；tree 命令时长取 `command.jsonl` 中同 case 的 `gridflux_tree_transfer`/`gridftp_transfer`；single hash 按上文 canonical tree 公式转换。当前恢复没有再执行这些计算。

## HIST-FORENSIC-RESTORE-01 执行回执

- **实际输入/输出 commit：** 输入、结束时 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。旧运行身份仍为 `16b3773 + dirty`。
- **恢复章节和来源：** 恢复原报告结论、raw 行数与 hash 指纹、dataset 与 canonical hash、匹配键和 58/14/38 分母、single/dense/mixed 分层表、计时边界、状态隔离、compression-off 污染、IO/resume 阻塞、候选问题和限制。来源为上一轮 02 分析结果/保留的聊天分析记录；04 简短复核确认 58/14/38、dense fp1–fp8 趋势及公平比较限制。
- **实际改动：** 更新本结果文件为完整报告；新增独立 `docs/tasks/2026-09-23-hist-forensic-restore-01-last-message.md`，与完整正文路径不同。未改其它文件、index 或旧证据。
- **无法恢复/仍缺：** dense GridFTP 样例 `case wall` 精确秒数没有留在可用的上一轮分析记录中，表内标 `—`，未猜值。04 结果文件当前是摘要，不含逐格独立重算日志；原始 payload 未保留，本轮不声称重新 hash。
- **下一步：** 00 验收恢复完整性；若架构选型需要严格比较，先冻结 01/04 的安全与计时契约，再由 05 实时确认环境、由固定提交矩阵产生新证据。04 的独立复核文件保持原样。
