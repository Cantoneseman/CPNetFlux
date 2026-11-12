# V3私有tmpfs首次大页机制54
日期2026-11-12；输入25ce05795749b7edb35eeb234a96fd29cfcaa26f，运行时代码79b49022。
方向决定：先验证目标后端首次分配粒度，而不是重复seed+collapse/POPULATE。
仅在root新建的私有mount namespace中挂一个专用有界tmpfs，
以huge=never/within_size对比首次页分配；实际文件操作降权回Su。
不修改全局shmem/THP、共享/dev/shm、路由/NIC/IRQ/RSS或其他服务。
原生RDMA和DEFERRED保持暂停。本轮不做跨机互传或正式M2矩阵。

首门限两个64MiB新文件（每次一个，挂载size=192MiB），真实不同字节写入/独立pread校验，
记录创建/ftruncate/map/首次写入/页型观察和清理；完整必要准备不移出任务计时。
不prefault/fallocate/seed/collapse；只比较首次写，不用暖页推吞吐。
检查实际挂载选项、映射地址、smaps实际ShmemPmdMapped、minor faults、CPU/墙钟、
MemAvailable≥12GiB与host pswpout增量0；Su子进程VmSwap0。
主机mount namespace/挂载列表/global shmem policy和外部服务前后相同，
finally仅卸载私有挂载并删除自有小payload。无任何共享挂载remount。

允许文件：本合同与对应结果、必要时docs/ROADMAP.md和docs/README.md；
原型/观测脚本及原始证据均在
/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-54，
不写运行时源码。原型只证明首次分配机制，不把64MiB内存写速率当V3成绩。
机制失败即停止，不改全局开关；若机制成立，另定完整新目标写入关键路径预算门，
局部≥15%才考虑V3实机合同；旧35/40Gbps/完整性/资源/网络门不放宽。

## 第一门结果与第二门固定合同（运行前）
64MiB机制门通过：never为16384 minor faults、0 ShmemPmdMapped；
within_size为32 minor faults、64MiB实际ShmemPmdMapped，均独立pread通过，
主机mountinfo/namespace/global shmem前后相同，所有文件和私有挂载清理。
单次耗时不作性能晋级。

第二门：复用当前同构建cpnetflux-mapped-cost-bench的ordinary lifecycle，
无seed/collapse，fresh 1GiB、4 workers、256KiB frame、profile off；
只改变私有tmpfs huge=never/within_size，挂载size=1280MiB。
五对AB/BA/AB/BA/AB，保留全部十次。固定判定：
每次content/bytes/map-unmap/major0/swap0/host pswpout0/12GiB/清理门；
worker_work（真实window map→close，包含观测、首次分配/写入）CPU和墙钟和中位至少降低15%，
两臂worker_work墙钟和population CV≤5%，至少4/5配对改善。
不得用whole中包含的unlink收益替代该门；whole含独立验证，仅辅助报告，
线程墙钟和相互重叠不称完整任务wall。页型已由第一门写后smaps独立核验；
现有bench页型观测发生在写前，不能拿该0值宣称大页未生效。
若门通过，仅准入下一完整V3合同，不宣告40Gbps/M2或进入8GiB矩阵。
