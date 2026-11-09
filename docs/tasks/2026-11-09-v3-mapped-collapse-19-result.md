# V3 Mapped Collapse19 结果与交接

日期：2026-11-09。**实际窗口诊断开发/测量完成；当前 seed + collapse 不晋级V3运行时。**
大页生效和minor faults下降已证实，但没有证实接收处理关键路径的足够收益。
完整文件吞吐仍采用scope17的27.291/27.296Gbps；本轮没有跨机传输或新的吞吐成绩。

## 输入、实现与代码备份

- 输入：`4b2bda724bccdbe7fea46e43bf28b5507bfff1da`。
- 开发/五对测量：`64ee2af199bca907c15e4e6416bcfd06e5fcf0fb`。
- 校验/销毁分项：`713623be27bc6f24a80630010b0ecb3870900e9f`。两者均已push并回读同SHA。
- remote：`git@github.com:Cantoneseman/CPNetFlux.git`；沿用 `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。
- R1根目录单人串行；保留两份October14未跟踪文档；没有代理、worktree或桌面操作。

新增 `cpnetflux-mapped-cost-bench --lifecycle-page-mode ordinary|collapse --profile off`，
只用真实MappedFileWindow、<=1GiB、1..4路、64MiB窗口。默认cold/warm模式保留。
collapse显式选择2MiB对齐：正常mmap若不对齐，先close，再用对齐地址作非固定提示，
内核不满足则关闭并报错；不使用MAP_FIXED，不额外保留地址空间。
默认writer及V3运行时没有启用新策略，核心只多了默认不选的映射对齐参数。

准备在独占span的完整2MiB区间建议HUGEPAGE、读取/写回同一字节、COLLAPSE；
没有清零原内容，没有认证written/verified进度。部分范围边缘、取消和失败可关闭并清理。
这不等于已经完成V3 lease/attempt、并发缺失range、恢复/发布集成。

页型不能仅看KernelPageSize。实际逐窗口smaps核验ShmemPmdMapped；
同文件多窗口可能合并VMA，诊断以mutex串行执行临时只读保护、smaps、恢复读写，
将当前窗口与其他RW窗口分开，避免重复归属其他lane页面。所有这些观测开销计入测量。
输出字段保守命名为 `shmem_pmd_mapped_bytes_lower_bound_sum`。

## 验收

新CLI先在旧程序因unknown option失败；helper的内容/取消/失败/边界红阶段失败后实现。
45/45相关CTest通过：9个既有writer、4个新helper/对齐、持久化range相关和两个诊断smoke。
显式 `--require-collapse` smoke：6个成功场景和11个负例通过，含实际大页、
非零pattern准备后保留、写后独立逐字节检查、1/4 workers及取消清理。
归因字段修改后两个诊断smoke再次2/2通过，require-collapse再次通过。

构建/测试命令：

```sh
cmake --build build --parallel 4 --target cpnetflux-mapped-cost-bench cpnetflux_unit_tests
ctest --test-dir build -R 'MappedFileWindowTest|MappedCollapseDiagnosticTest|PersistentMappedRangeTest|PersistentRangeWriteTest|cpnetflux_mapped_(cost_bench|lifecycle)_smoke' --output-on-failure
python3 tools/test/run_cpnetflux_mapped_lifecycle_smoke.py --build-dir build --require-collapse
```

`tests/acceptance.log`保存实际45条测试。原始失败也保留：错误的边缘大小测试断言已修正；
smaps合并VMA导致的归属断言已用上面的隔离观测修正。这些不是通过的性能样本。
未强制注入“内核忽略对齐提示”和对齐重试的第二次mmap失败；进入运行时前仍须补此类确定性故障门。

## 五对固定版本测量：局部门未过

每个进程1GiB新tmpfs文件、4 workers、65532B有效写粒度、profile off；五对AB/BA交错，
全部十次保留，不删慢样本。whole包括create/resize、线程/缓冲、map/对齐重试、
seed/collapse、权限/smaps观测、write、unmap、独立pread/逐字节验证、close/unlink。
worker_work是每窗口借用映射到关闭的CPU/墙钟和，含观测、不含写后验证；线程墙钟和会重叠。
这两个指标都不是CPNetFlux完整传输T_ready。

| 指标，中位数 | ordinary | collapse |
|---|---:|---:|
| whole诊断墙钟 | 329.591ms | 243.756ms |
| whole CPU和 | 1.057299s | 0.953733s |
| whole墙钟CV（population） | 5.124% | 0.949% |
| worker准备/写入/关闭CPU和 | 0.643919s | 0.650689s |
| worker准备/写入/关闭墙钟和 | 0.655129s | 0.659553s |
| worker minor faults | 262158 | 533 |
| copy阶段minor faults | 262144 | 0 |
| prep CPU和 | 0 | 0.396506s |
| 每次页型观测下界总和 | 0 | 1GiB |

whole中位墙钟下降26.043%，配对下降23.59/19.63/30.74/29.78/25.06%，5/5为正。
但ordinary CV超过预设5%，不能四舍五入判通过；worker CPU/墙钟没有稳定收益。
局部复制CPU下降，被额外0.397s/GiB大页准备基本抵消。fault次数减少99.8%不能冒充同幅时间收益。
**局部门失败，不做V3集成，不重跑至CV好看为止。**

## 两对补测：收益主要发生在删除自有目标

同规模固定713623b，仅增加写后验证和close/unlink计数，不改变执行顺序。
两对用于解释上一节，全部保留；不与原五对合并验收，不因补测CV较低翻转失败结论。

| 指标，中位数 | ordinary | collapse |
|---|---:|---:|
| whole墙钟 | 344.955ms | 243.245ms |
| 准备/写入/关闭CPU和 | 0.659454s | 0.644703s |
| 准备/写入/关闭墙钟和 | 0.667940s | 0.653235s |
| 写后验证CPU和 | 0.320000s | 0.295412s |
| close/unlink墙钟 | 93.338ms | 3.136ms |
| close/unlink CPU | 0.093128s | 0.003137s |

两臂whole墙钟差约101.710ms，串行close/unlink差约90.202ms，占该差约88.7%。
这是仅两对补测的归因观察，不是稳健比例估计；各线程墙钟不能再与whole相加。
处理关键路径只有小幅变化，与原五对基本不变一致，尚无>=15%关键路径预算。
真实传输成功后保留目标，删除实验输出通常不应计入T_ready。因而这次局部whole下降
不能用于预测完整传输提升；下一阶段诊断必须同时列成功可用边界和销毁清理边界。

## 资源、环境、清理和适用范围

- 两轮最小MemAvailable分别约27.15/27.54GiB，均>=12GiB；owned VmSwap峰值0，host pswpout增量0。
- 真实同时映射每路<=64MiB，总<=256MiB；每个进程文件<=1GiB，逐次验证后close/unlink。
  两个mkdtemp目录均已删除，raw目录仅日志/JSON/脚本，无payload。
- R1生产PID126766/start1191040保持；R2只读核对PID164834/start1679976保持。
  R2仍在7dbaa06，未同步此本机诊断；没有使用不同版本进行互传。
- R1 kernel7.0.0-38-generic、PMD2MiB、shmem [never]、THP [madvise]；实际挂载与二进制/
  CMakeCache SHA见measurement-input.json；不改共享THP、mount、NUMA、网络或生产服务。
- 没有本轮owned TCP流量；scope17 rx_missed_errors+94仍未关联，不能宣告网络门通过。
  RDMA继续暂停；tmpfs结论不能外推物理磁盘或持久化性能。

## 路线和下一项交接

LF4这一个真实窗口 `seed + collapse` 候选结束，不继续调种子、窗口/并发或重复population，
不集成V3。保留诊断能力和大页可行性证据；若以后有能减少零页初始化/复制的新机制，
须重新提出关键路径预算，不能只用fault下降重开。LF1/2大帧与LF3 CRC仍是预算未证实、
暂缓运行时，未据此宣告它们被淘汰。

下一项进入LF5逐字节搬运成本核对，先处理已知发送端pread成本：

1. 对实际发送源读取、CRC扫描、帧/队列缓冲和sendmsg的拥有者、次数、字节数建表；
   区分逻辑复制字节与实测DRAM流量，CPU样本比例不能当作可消除墙钟。
2. 首个可筛选的新机制是驻留源只读有界映射，研究能否省掉pread到发送buffer的一遍复制。
   先确认现有源码是否已有等价路径；保持CRC32C、chunk/范围和只读源有效性检查。
   不重开sender-inline或直接接收。
3. 若缺证据，只新增<=1GiB、真实多样字节的有限源端诊断，对照actual PosixFile::readAt
   与只读映射消费，计入map/首次PTE/CRC/借用到关闭；源准备与销毁单列。
   同一CRC结果、完整字节数、CPU及关键服务区间先有>=15%可信预算才设计V3集成。
4. V3集成前必须解决queued payload的窗口所有权/pin、异步消费者完成、取消/部分写、
   源变更和64MiB/lane总预算，禁止借用后立即解除映射。TCP零拷贝另有完成通知设计门，
   不能只加MSG_ZEROCOPY。无新预算则停止该机制，转S0小文件独立基线。
5. 大文件正式保留门仍是同提交双端五对新8GiB、task_rate中位>=10%、CV<=5%、
   >=4/5配对正收益及CRC/SHA、恢复、资源、网络门；30–50G/70–85G/100G未达。

全部原始证据：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-mapped-collapse-19`。
任务/实现范围见[本轮任务](2026-11-09-v3-mapped-collapse-19.md)，统一入口见[路线图](../ROADMAP.md)。
