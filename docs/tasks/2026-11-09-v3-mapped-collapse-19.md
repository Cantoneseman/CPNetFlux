# V3 Mapped Collapse19：实际有界窗口诊断

日期：2026-11-09。输入 `4b2bda724bccdbe7fea46e43bf28b5507bfff1da`。状态：开发/验收/有限测量完成；局部门未过，当前seed+collapse不晋级运行时。
结果与下一项见[结果记录](2026-11-09-v3-mapped-collapse-19-result.md)。

## 范围、非目标与受影响文件

承接 Byte Work18 的16MiB可行性探针，使用真实 MappedFileWindow，默认映射保持不变；增加仅诊断显式选择的2MiB对齐策略。
新增显式 lifecycle-page-mode ordinary/collapse 诊断：只在当前独占窗口内的完整2MiB对齐区间
进行 MADV_HUGEPAGE、保留原字节的触页及 MADV_COLLAPSE，边缘保持普通页。
准备并不认证任何数据；这里只拥有新建自有文件的完整窗口，不借此声称解决运行时 lease/attempt、
checkpoint/resume 或共享目标的并发所有权。局部成功后另立 V3 集成范围。

不改变核心 writer 默认行为、V3 runtime、CRC、网络、RDMA、默认参数、系统THP/mount、生产服务。
不跨机互传；R2 不改源码。本轮 R1 单人串行，不用子代理或 worktree。
保留两份 October14 未跟踪文档。

受影响文件另含 include/cpnetflux/storage/mapped_file_window.h、src/storage/mapped_file_window.cpp。
受影响文件：本任务、结果、ROADMAP/后续计划；CMakeLists.txt；
tools/perf/cpnetflux_mapped_cost_bench.cpp、mapped_window_collapse.h、mapped_window_lifecycle_bench.cpp；
tools/test/run_cpnetflux_mapped_lifecycle_smoke.py；tests/unit/mapped_file_window_test.cpp。

## 实现与计时合同

- 兼容原 cold/warm 诊断；新模式只接受总量<=1GiB、1..4 workers、每路64MiB整数窗口。
- 映射数据<=64MiB/路，4路<=256MiB，不额外保留对齐地址空间；真实地址向内对齐，记录边缘。
- whole_diagnostic 包含创建/resize、线程/缓冲准备、mapping、seed/collapse、实际 writer.write、
  smaps观察、unmap、独立pread验证、close/unlink。worker_work 包含每窗口从借用映射到关闭，
  包含 smaps 观察，排除验证；线程墙钟求和不能当整任务时间。
- 内容模式 fresh/pattern：fresh 不通过预读/预写提前分配所有页；pattern 准备前已写非零模式，
  准备后、覆盖前独立检查原内容。初始化在 whole_diagnostic 内，单列秒数。
- helper 使用独占 span 的内向对齐区间，逐2MiB读取并写回同一字节，禁止盲目清零；支持取消检查。
  建议/折叠失败明确报错，不把失败当普通模式成功，RAII退出关闭映射并删除自有文件。
- 核验实际 ShmemPmdMapped；KernelPageSize/MMUPageSize 不能单独判断页型。

## 验收与实验门

1. 新接口 smoke 在旧程序明确失败，再实现；helper 单测覆盖原字节、边界/边缘、取消、
   HUGEPAGE/COLLAPSE 失败和 writer 关闭/不认证；旧 mapped smoke 和相关单测不回退。
2. cmake --build build --parallel 4 --target cpnetflux-mapped-cost-bench cpnetflux_unit_tests
   ctest --test-dir build -R 'MappedFileWindowTest|MappedCollapseDiagnosticTest|cpnetflux_mapped_(cost_bench|lifecycle)_smoke' --output-on-failure
3. 代码提交、推送及远端SHA回读后，固定提交运行<=1GiB/4路、至少五对交错本机A/B。
   局部门：whole_diagnostic中位墙钟下降>=15%，两臂CV<=5%，至少4/5配对改善，
   worker CPU及fault机制一致，内容/清理/资源通过，实际大页有效。未过不集成。
   该门只许可V3实验集成，不代表完整文件吞吐提升。
4. MemAvailable>=12GiB、owned VmSwap=0、host pswpout增量0；保留生产PID/starttime；
   mkdtemp自有tmpfs目录精确清理。不更改THP/mount/网络。记录当前设置、源码/二进制SHA、
   测量驱动、全部原始样本/失败和分析；证据目录：
   /home/Su/experiments/CPNetFlux/2026-11-09-v3-mapped-collapse-19。
5. 完整吞吐仍采用scope17约27.3Gbps；正式保留须未来同提交双端五对8GiB、
   中位吞吐>=10%、CV<=5%、4/5正收益及完整性/恢复/资源/网络门。接口+94未解决。

## 开发前对齐修正

实际普通64MiB映射地址 mod 2MiB=798720，不能保证虚拟/文件偏移同时PMD对齐。
因此诊断策略先正常映射；若未对齐，close后用向下2MiB对齐地址作为非固定mmap提示。
三个无payload原生mmap探针均获得对齐地址。内核若忽略提示则明确失败/关闭，不使用MAP_FIXED，
不覆盖其他映射，不增加窗口峰值。增加的mmap/unmap必须计时和计数。默认构造行为保持原样。

## 页型归属观测修正

强制实际大页smoke发现：相邻同fd窗口可合并为一个VMA，旧exact-VMA断言不可靠；
仅用VMA总ShmemPmdMapped会把其他lane重复计数，保守下界又可能暂时无法证明当前lane。
诊断观测时以进程内mutex串行执行mprotect(PROT_READ)、读取本窗口独立VMA、恢复读写。
不读/预分配fresh数据；不在读保护期间写入。权限切换和mutex/smaps开销全部计时。
只用于观测，未来V3集成不沿用这段观测开销。异常也由RAII恢复权限/关闭窗口。

## 开发门结果

新接口在旧程序因unknown option失败；helper红阶段三个内容/失败/边界case失败，
对齐case在/tmp普通文件系统中可能天然对齐，不能以它证明tmpfs原始地址已对齐。
实现后修正测试误把8MiB减去边缘视作仍能容纳8MiB完整PMD的断言。
45/45相关CTest通过（原9个writer、新4个helper/对齐、持久化range相关和两个诊断smoke）。
显式require-collapse smoke通过：6个成功场景（fresh、pattern、1/4 workers）和11个负例，
包括真实PMD、覆盖后独立逐字节检查及取消清理。诊断观测修正前失败日志保留。
未向V3 runtime开放对齐/大页选项；没有跨机payload。下一步固定提交本机五对测量。

## 五对后有界归因补测（不重开收益门）

64ee2af五对中whole墙钟下降26.04%，但ordinary CV=5.124%超5%，且worker CPU
0.643919→0.650689s/GiB、worker墙钟和0.655129→0.659553s/GiB基本不变。
因此门未过，不接入V3。不重跑挑选较好CV。
在同一工具增加输出校验及close/unlink独立CPU/墙钟计数，默认输出不改变执行顺序；
相关smoke核验新字段，提交固定代码后仅做两对归因补测。保留原五对，不合并成新收益样本。
目标是确认whole收益是否主要来自校验/销毁目标，而非接收关键路径；不扩大payload/并发。
