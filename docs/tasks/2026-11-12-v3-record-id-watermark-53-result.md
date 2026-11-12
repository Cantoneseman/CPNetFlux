# V3范围记录ID水位53结果
日期2026-11-12；输入81a34b7c7bae2430c61c8c56e9019b8df539bab6。
实现79b49022b7d980bd5b9cee3b13261c6481a26832，已推送并回读GitHub SHA。
状态：工程修复/局部工作量验证通过；M2与网络稳定性OPEN。

## 实际修复
FileTransaction::recordRange原先每个完成chunk遍历全部verifiedChunks计算max ID+1；
8192个chunk累计33550336条记录访问。两种会话本已维护append watermark，
新nextVerifiedChunkId从该水位计算，真正范围完成路径已改用它。
只减少“分配记录ID”的重复遍历；乱序recordVerifiedChunk的重复身份检查仍保留，
不能把整段manifest处理都称为O(1)或线性化。未新增索引表/线程/缓冲/协议选项。

调用方继续在既有Entry锁内串行查询并记录，查询不是ID预留，不提供并发分配器。
空manifest返回0，含0记录后返回1，稀疏/乱序取最大值；
UINT64_MAX仍是耗尽哨兵，最大现有值为MAX-1或MAX均拒绝新范围ID。
None校验的旧规则保持；save排序/move不改变水位；
resume和损坏剔除沿用原有rebuild，所有记录剔除后重置为0。
CRC、校验帧、attempt/lease、checkpoint格式/节奏、恢复和发布不改。

## 红绿灯与回归
先以旧扫描算法实现相同只读查询并加观察计数；
两类会话的复杂度回归实际各读33550336条，阈值8192，测试exit1。
另四项语义用例通过；红灯不是缺少API的编译失败。
改为水位后两类会话共18项通过，含8个新用例：
顺序复杂度、稀疏/乱序/save/resume/move/耗尽、None、剔除最大ID及全部剔除重建。
最终增加MAX恢复断言后18/18重跑通过。
R1/R2相关CTest各251/251通过，95.57/95.83s，
覆盖session/persistent、CRC、取消/deadline、预算、大帧及中断恢复/publication。
不是全仓库全部测试。R1全目标构建与R2同提交构建成功，配置及三个二进制SHA一致；
git diff --check通过。仅新增测试断言/源码换行保持后重跑直接18项，
没有无理由重复整套回归。原始日志和退出码保留。

## 局部成本量化
最初的基线/新查询探针启用了工作量计数，保留为机制证据。
随后用同一新二进制中的原扫描reference与新查询，双方关闭观察统计，
避免逐记录计数污染成本对照；三对AB/BA/AB。
只测会话内记录插入+ID选择，不save、不写payload、不走网络、不建8GiB文件。
offset代表1MiB chunk，但不代表实际传输了该规模数据。

| 角色/记录数 | 扫描中位墙钟 | 水位中位墙钟 | 扫描中位CPU | 水位中位CPU |
|---|---:|---:|---:|---:|
| upload / 1024 | 0.301465ms | 0.028139ms | 0.301478ms | 0.028154ms |
| download / 1024 | 0.301180ms | 0.029234ms | 0.301221ms | 0.029351ms |
| upload / 8192 | 18.046643ms | 0.293750ms | 18.046634ms | 0.293841ms |
| download / 8192 | 17.961103ms | 0.268892ms | 17.961288ms | 0.268896ms |

这证明局部工作消除；探针不含真实Entry争用、范围调度、网络、写入和checkpoint。
不能将约18ms直接从历史完整T_ready相减，不能由此宣布跨过40Gbps或新正式成绩。
修复无额外payload资源、格式或复杂索引，作为确定冗余消除保留；
没有为追小收益重跑已停止的1GiB/8GiB跨机矩阵。

## 数据所有权审查与下一方向
当前大帧无压缩路径的逻辑工作（不是DRAM总线实测）：
- sender：pread把源页缓存复制到复用vector；每逻辑字节一遍ISA-L CRC32C扫描；
  准备队列移交vector所有权，writeFrame用header/prefix/payload三个span发送，
  没有为CRC前缀额外拼接整帧。普通sendmsg仍需用户到内核TCP的数据搬运。
- receiver：读取到连接复用的payload vector；CRC前缀仅subspan，
  一遍CRC验证后向state/range传递已验摘要，不为这些层重复扫描payload；
  inline通过move/归还vector，没有逐帧逻辑解码复制或prefix memmove。
- 目标：MappedFileWindow::write仍有payload到新tmpfs映射的一遍memcpy；
  包含首次目标页分配/清零及PTE建立。先前直接接收仅消除该用户复制，
  而未消除页工作，已未过收益门。队列回收保留vector.size，常规同长resize不清零整帧。
- 本轮ID修复不改变这些字节工作，也不解释Task52共享接口OOB事件。

仍需针对“改变实际新目标页分配粒度”提出不同于seed+collapse/POPULATE的新机制。
值得下一步检查的是进程私有mount namespace中的专用、有容量上限的tmpfs huge=within_size：
若能首次分配大folio，可避免先分4KiB页再seed/collapse的额外准备。
这只是待验证假设，不是已测收益或默认建议。内核文档分别支持tmpfs专用挂载
与THP策略的机制说明：[tmpfs](https://docs.kernel.org/filesystems/tmpfs.html)、
[THP](https://docs.kernel.org/admin-guide/mm/transhuge.html)；实际内核必须核验挂载选项和页型。
已核对R1：全局shmem为never、THP madvise；不改它们。
无特权user namespace因uid_map权限失败，sudo -n -l确认Su有NOPASSWD权限。
后续可用root创建仅子进程可见的私有mount namespace，再降权运行诊断；
不修改全局THP、/dev/shm挂载或共享网络，不把预分配移出计时。
实施前另写受限scope；先做本机新文件机制/准备计时、页型与资源/内容门，
达到明确局部关键路径预算才考虑完整V3集成及新的同提交对照。
原生RDMA和DEFERRED项仍暂停；通用profiler、frame/lane/rate扫描不重启。

## 验收命令与交接
~~~bash
cmake --build build -j4
build/cpnetflux_unit_tests --gtest_filter='*SessionRecordLookupTest*'
ctest --test-dir build --output-on-failure -R 'SessionRecordLookup|TransferSessionTest|DownloadSessionTest|Persistent|Deadline|Timeout|Cancel|Budget|Publication|cpnetflux_large_data|cpnetflux_session_record_bench_smoke|cpnetflux_v3_.*resume|cpnetflux_publication_resume'
~~~
证据根/home/Su/experiments/CPNetFlux/2026-11-12-v3-record-id-watermark-53：
red-exit.json/red.log、green-unit/final-unit、两端构建/回归、
probe.cpp/probe-matched.cpp、原始jsonl和probe-summary.json、builds-matched.json与delivery.json。
没有创建payload，无新互传；旧Task52计数失败保留，历史M1仍为8GiB中位34.159Gbps。
旧交付C为Task51/52冻结快照，不自动代表本次新运行时代码；本次代码以79b49022为准。
两份既有未跟踪文档保留，未提交凭据/构建产物/实验payload。
