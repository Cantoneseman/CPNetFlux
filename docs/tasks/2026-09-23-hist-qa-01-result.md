# HIST-QA-01：旧实验取证结果独立复核

任务版本：HIST-QA-01 v1；路线版本：R2026-09-23.3。
本文件恢复上一轮 04 已完成的独立复核正文，供后续设计审查追溯。恢复本身不重新扫描 216 行数据、不重算 payload，也不升级历史数据的性能资格。

## 结论

历史数据足以提出一个窄的目录路径源码映射候选：深圳到上海的 dense tree 上传中，CPNetFlux 的 client-process goodput 随 file parallelism 从 1 增至 8 明显上升，而 GridFTP 对照基本持平。这说明低文件并行度下可能存在供给或扩展不足，值得定位目录 worker 的下一文件准备和数据路径阶段。

这些数字不构成公平的 CPNetFlux/GridFTP 性能结论。GridFTP 使用 GSI 并在命令中启用 data-channel privacy；CPNetFlux 样例记录 anonymous auth 且 data TLS 为 off。选入诊断表的 CPNetFlux tree 记录还被旧 classifier 标记为 fail_correctness，理由涉及 wire accounting 和缺失的 verified_chunks 证据。旧 CSV 的 hash_match 与 exit code 不能替代 payload 独立重算，也不能把旧失败标签改写成新验证通过。按完整配置匹配口径，严格 performance_eligible 样本数为 0。

## 审查范围与证据来源

上一轮审查读取的是历史结果文件，不读取 payload：

- 原始结果目录：D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results。
- 主要文件：results.csv、command.jsonl、environment.json、case_plan.json；相关解释另对照旧 runner 日志和代表性客户端记录。
- 当前恢复的 02 完整结果：docs/tasks/2026-09-23-hist-forensic-01-result.md。该文件说明其报告来自先前 02 核验记录，本轮恢复没有重扫原始矩阵。
- 本次 QA 原结果原先被 CLI 摘要覆盖；现存摘要只作为此前 QA 结论的旁证。本报告数值恢复自上一轮 04 已完成的读取、复算与审查记录，不把当前恢复任务伪装成一次新审计。

上一轮 QA 记录的关键输入指纹如下：

| 文件 | SHA-256 |
| --- | --- |
| results.csv | F84888CC899CF291A6B65E088958D3B201AF462998E8BEFC9DFE64C33AE58E86 |
| command.jsonl | F18EE6C484BAB72C5F652B85A455EA4C609F879AD197651E46B561EF03D71C6E |
| environment.json | 093129CCDD849780B4AA82876395232D0493A1545AE1FE4DAE209EB79EF1B0C8 |
| case_plan.json | DE351FC1B8EB506378D454DD93C9B55D59A2F73E259F2BCD67B38CAF7A5CFD42 |

旧运行身份是 commit 16b377359494f19f386ba5d375d353449e45f7a0 加 dirty 修改。02 恢复报告记录该工作树有 78 条非空状态行。因此，即使历史输入文件指纹可核对，这批结果也不是仅凭 commit 可重建的固定构建产物。

复核使用了 PowerShell 对结果文件的只读读取与 Get-FileHash 指纹核对，CSV 由 Import-Csv 处理，JSONL 逐行用 ConvertFrom-Json 读取；配对和 goodput 按下文字段与公式复算。原 QA 轮次读取了结果记录和日志，但没有读取或恢复 payload。本恢复轮没有再次执行这些分析命令。

## 配对口径与 58/14/38 计数

旧 plan 有 218 个唯一 case_id；results.csv 有 216 行且 case_id 唯一。两项未返回结果的计划项是 GridFTP mixed-tree resume 双向 case。历史 core 有 144 行，CPNetFlux 与 GridFTP 各 72 行。72 个跨系统候选配对由 single、dense、mixed 各 24 个组成。

匹配键为 dataset、direction、file_parallelism、per_file_connections 和 repeat_index。候选配对还要求源内容记录一致、双方 exit_code 为 0、hash_match 为 true，且 max_observed_data_streams 相同。single 的系统间 hash 表示不同：CPNetFlux 记单文件 SHA-256，GridFTP 记单文件 tree hash；上一轮按 relative path、字节数和文件 SHA 的既有 canonical 规则规范化后，24 个 single 键相符。dense/mixed 共 48 个键的源/目的 tree hash 逐字相同。此项是既有运行时 hash 记录的口径对齐，不是本轮或上一轮 QA 独立读取 payload 后重算 SHA。

| 复核项 | 复算结果 | 口径与判定 |
| --- | ---: | --- |
| 跨系统候选键 | 72 | single 24 + dense 24 + mixed 24 |
| stream 不匹配的 CSV 行 | 15 | 一组候选键的两侧都触发 mismatch，因此行数多于排除键数 |
| stream-count exclusion 键 | 14 | single 下载 r0 四个连接档 4；dense 下载 fp1/c1 r0 1；mixed 下载 c2 下 fp1、fp2、fp4 各三次 9 |
| runner-case-wall 配对 | 58 | 72 - 14；只作历史 wall 诊断，不是纯传输时间 |
| tree client-process 候选键 | 48 | dense 24 + mixed 24 |
| tree stream 不匹配排除键 | 10 | dense 下载 1 + mixed 下载 9 |
| tree client-process 配对 | 38 | 48 - 10；dense 23 + mixed 15 |

计数结论：58、14、38 的候选分母和扣除关系一致。15 是不匹配 raw rows 数，14 是去重后的配对键数；两者不能混为一谈。对计数复算给 PASS，不表示这些配对具备公平性能资格。

goodput 统一使用十进制 Mbps：logical_bytes × 8 ÷ duration_seconds ÷ 1,000,000。配对比是同一 repeat 的 CPNetFlux Mbps / GridFTP Mbps，再报告每格中位数；旧 repeats 是按系统连续运行，不是随机交错 block。每格通常只有 2–3 次重复，range/median 仅描述离散程度，不构成显著性检验，也不能排除链路时间变化混杂。

## Dense 深圳到上海上传：client-process 复算

数据集为 128 MiB，即 134,217,728 logical bytes、128 个 1 MiB 文件；方向 local_to_remote；每文件连接数为 1。时间来自 command.jsonl 中同一 case 的 gridflux_tree_transfer 或 gridftp_transfer subprocess duration_seconds。客户端进程窗包括各自扫描/规划、控制交互、数据传输与结束等待；独立 source/destination hash 在进程计时窗外。它不是 socket payload-only 时间。

下表保留上一轮 04 逐 repeat 复算的时间与由逻辑字节重新计算的 goodput。每个 repeat 的配对比按该 repeat 的两侧时长计算。

| 文件并行度 | 工具 | repeat 0 秒 / Mbps | repeat 1 秒 / Mbps | repeat 2 秒 / Mbps | 中位 Mbps |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | CPNetFlux | 59.898555 / 17.926 | 59.301648 / 18.106 | 58.895891 / 18.231 | 18.106 |
| 1 | GridFTP | 11.081363 / 96.896 | 11.481947 / 93.516 | 11.832550 / 90.745 | 93.516 |
| 8 | CPNetFlux | 10.782753 / 99.580 | 10.585899 / 101.431 | 10.531688 / 101.953 | 101.431 |
| 8 | GridFTP | 11.381711 / 94.339 | 11.132048 / 96.455 | 11.282314 / 95.170 | 95.170 |

| file parallelism | 每 repeat 的 CPNetFlux/GridFTP goodput 比 | 配对比中位数 |
| ---: | --- | ---: |
| 1 | 0.1850、0.1936、0.2009 | 0.1936 |
| 8 | 1.0556、1.0516、1.0713 | 约 1.0556 |

在 CPNetFlux 内部，fp1 到 fp8 的 client-process goodput 中位数约提升 5.60 倍；GridFTP 对应中位数约从 93.516 到 95.170 Mbps。这个斜率可以支撑低文件并行度下供给不足作为源码映射线索。它没有定位供给间隙是目录计划、worker 排队、连接准备、服务端等待、磁盘、校验或其他阶段造成。

## Dense 结果的 wall timer 交叉核对

results.csv 的 elapsed_seconds 是 runner-case-wall，而不是上表的客户端进程时长。上一轮 QA 对 fp1/fp8 的三次记录分别复算如下：

| 文件并行度 | 工具 | CSV elapsed_seconds（三次） | wall goodput（三次，Mbps） | 配对 wall ratio（三次） | 中位配对比 |
| ---: | --- | --- | --- | --- | ---: |
| 1 | CPNetFlux | 65.099958、64.291328、63.994278 | 16.494、16.701、16.779 |  |  |
| 1 | GridFTP | 13.080587、13.475165、13.722953 | 82.087、79.683、78.244 | 0.2009、0.2096、0.2144 | 0.2096 |
| 8 | CPNetFlux | 15.910836、15.760996、15.791576 | 67.485、68.127、67.995 |  |  |
| 8 | GridFTP | 13.283987、12.976749、13.167988 | 80.830、82.744、81.542 | 0.8233、0.8339、0.8349 | 0.8339 |

wall 与 client-process 给出的相对判断明显不同：dense fp8 client-process 配对比约 105.6%，runner-wall 配对比约 83.4%。因此 runner wall 不能冒充纯传输/共同事件窗口；timer 分母对结论有实质影响。

## GridFTP 调用、配置差异与单文件边界

GridFTP 对照是外部 globus-url-copy。历史 tree 上传命令示例使用 -fast、-dcpriv、-cd、-rp、-p、-r、-cc 等参数；environment.json 记录 GridFTP auth mode 为 GSI。相应 CPNetFlux 样例使用 anonymous auth，客户端摘要记录 data_tls_mode 为 off，控制 TLS 也未与 GSI 建立等价配置。因认证和数据保护条件不同，stream 数相同不代表安全与协议条件可比，58/38 个 pair 仍不能标记为 performance_eligible。

GridFTP CSV 上的 file_io_backend=POSIX 是 runner schema 标签，不能推断外部 GridFTP 有与 CPNetFlux 同样受控的 POSIX 实现配置。GridFTP 的单文件沪→深多连接路径还拆为多个 gridftp_partial_get 子命令，不等同于 CPNetFlux 单文件多连接路径。

单文件命令计时不能纳入同口径 client-process goodput。CPNetFlux runner 先发出 STOR/RETR，再启动文件客户端；客户端退出后 runner 才读取最终控制回复。GridFTP CLI 则自行包住控制和数据过程。CSV outer wall 同样含准备/收尾边界，不能代替 payload-only timer。现存日志没有可恢复的共同 socket payload 区间或等价 native timer，因此单文件的正式配对计时结论为 PARTIAL / 不可配对。

## Integrity、evidence 与 wire accounting 的隔离

旧矩阵总分类记录为 124 pass、68 fail_correctness、18 blocked_io_uring、6 fail_runtime，另有 2 个计划 case 无结果。上一轮结果核对的 68 个 fail_correctness 行均为 exit code 0 且 CSV hash_match=true；其中 39 行同时记录 wire 字段不一致与 verified_chunks 缺失，29 行记录 wire 字段不一致。它们解释为分类/计量证据问题，不能直接称作 payload 损坏，也不能因此改判 pass。

本复核选入的 CPNetFlux dense tree 行仍保留 fail_correctness 标签，错误文字指向 wire_bytes 未等于 logical_bytes、verified_chunks 证据缺失。被选样例记录 128/128 文件完成且 source/destination tree hash 字段相同，client summary 也记载 128 文件完成；这些是旧运行记录，不等于本轮独立验证 payload。传输退出状态、内容 hash 记录、wire accounting 和验证证据是不同维度。缺少经独立复核的 integrity/evidence，不能只凭良好 throughput 数字做正确性结论。

18 个 blocked_io_uring 是旧后端前置条件未满足；6 个 fail_runtime 中有资源不足/ENOSPC 记录；2 个 GridFTP resume 计划项无结果。它们是各自的阻塞/缺失状态，不可并入性能通过样本或解释成零时长。

## 可以支持与不能支持的推论

支持：dense 深圳到上海 tree 上传中，CPNetFlux 单客户端配置随 file parallelism 增加表现出明显的内部并发响应；这足以把目录低并行供给路径交给后续源码映射，优先观察 worker 拿取下一文件、准备阶段、每文件数据启动、first payload 与完成等待。若做 profiling，应把 first_payload 与 payload_io 作为独立区间，记录并发 worker；阶段可重叠，不能把各阶段时长相加当 wall time。

不支持：该历史样本不能证明核心瓶颈是 syscall、sendv、scheduler、control reuse、TCP/TLS setup、manifest rewrite、磁盘、CRC/hash 或远端服务；不能证明 CPNetFlux 在公平 GSI/data-privacy 条件下达到或超过 GridFTP；不能证明 payload hash 已由本轮重算；不能从 dirty 旧运行推断当前 HEAD 的性能；不能外推到 100 G、生产 readiness 或其他并行度/方向。

窄的下一步候选是把“目录文件并行度低时供给不足”交给源码映射与阶段观测，而不是按这份 QA 直接授权优化。先测 scan/plan、control acquire/prepare、data connect、first payload、payload_io、完成等待及 manifest finalize，并保留重叠关系；随后由 01 冻结计时/安全比较契约、由 00 决定是否派发实现任务。此历史证据本身不授权代码或实验。

## 复核状态与未运行项

| 项目 | 结论 | 依据 |
| --- | --- | --- |
| 58 runner-wall pair 及 14 exclusion 键 | PASS（计数） | 72 个候选键扣除 14 个唯一 stream mismatch 键；15 是 raw mismatch rows |
| 38 tree client-process pair | PASS（计数） | 48 个 tree 键扣除 10 个不匹配键；dense 23、mixed 15 |
| Dense fp1/fp8 数值 | PASS（既有结果复算） | 依据 command duration、logical bytes 与 repeat 配对；wall 数另表区分 |
| CPNetFlux/GridFTP 公平比较 | FAIL / performance_eligible=0 | GSI + data privacy 与 anonymous + data TLS off 不匹配 |
| 单文件共同计时 | PARTIAL | 双方命令窗口不同；没有共同 payload-only 时间 |
| 低并行度供给不足候选 | PARTIAL，值得源码映射 | 只见并发斜率，原因未定位，旧正确性分类仍保留 |
| payload 独立验证 | NOT_RUN | 没有读取或重新 hash payload |

原 QA 轮次未运行 CMake、CTest、当前 runner、build、transfer、benchmark、新实验或 SSH。HIST-QA-RESTORE-01 本轮也未运行这些项目；本轮只是恢复已完成的复核文本和执行文档门禁。没有结果的测试保持 NOT_RUN，不写通过。

## HIST-QA-RESTORE-01 执行回执

- 实际输入/输出 commit：输入 HEAD 和恢复结束 HEAD 均为 a076c532640ba06de016ed7ed20f7d2a6d48a0a7。
- 恢复章节与原始复核来源：恢复范围/数据指纹、候选与配对口径、58/14/38 计数、dense client-process 与 wall 数值、auth/data-protection 配置差异、单文件 timer 边界、正确性/wire 计数分类及候选边界。来源是上一轮 04 已完成的原始 CSV/JSONL 读取和复算记录、当前 02 恢复报告中公开的表/约束、前序 04 聊天中保留的逐 repeat 数值；本轮未重新扫描完整原始数据。
- 实际改动：仅恢复本结果文件，并在独立的 docs/tasks/2026-09-23-hist-qa-restore-01-last-message.md 写本次摘要。未修改其他文件；未操作 Git index。
- 实际门禁与退出码：恢复后执行 UTF-8 严格解码、BOM/尾随空白检查、git diff --check、HEAD 前后核对、暂存区核对；实际命令及退出码在本节记录与摘要中复核为 0。
- 无法恢复/仍为 PARTIAL：单文件两端共同 payload-only timer 不存在；旧配置不等价；fail_correctness 不能被改判；payload 未独立重算；重复顺序未随机化；阶段因果和瓶颈仍未确认。历史记录中未保留的精确 case wall 秒数不猜测。
- 后续任务状态：历史审查全文恢复不等于性能验收。等待 00 验收；此后再按 BOARD 串行进入 LOWLEVEL-DESIGN-QA-01，不因此授权实现、测试、构建或跨域实验。
