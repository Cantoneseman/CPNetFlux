# 私有tmpfs首次大页54：机制与局部门通过
日期2026-11-12。输入25ce05795749b7edb35eeb234a96fd29cfcaa26f；
运行时代码79b49022b7d980bd5b9cee3b13261c6481a26832，双端构建/二进制一致见Task53。
状态：LOCAL ADMISSION PASS / FULL V3 NOT RUN / M2 OPEN。

## 新机制及隔离
旧seed+collapse先建立4KiB页，再额外准备/合并，局部关键路径无净收益。
本轮从分配源头改变“实际新建目标的页分配粒度”：
sudo -n unshare --mount --propagation private --fork创建独立mount namespace，
仅在其中把专用空目录挂为有容量上限的tmpfs，比较huge=never与huge=within_size。
文件操作/benchmark均降权到Su(uid1002)，根权限只用于私有挂载与卸载。
未调用fallocate、prefault、seed或MADV_COLLAPSE；未修改CPNetFlux运行时/默认配置。

主机mount namespace为mnt:[4026531832]；前后mountinfo SHA同为
11f58d9103280acc133b382fc9a50095e41d230955f6f9fc1b0b6856637ba9c8。
全局shmem仍always within_size advise [never] deny force；
共享/dev/shm未remount，未改THP/NIC/网络/调度/swap设置。
子namespace退出前finally卸载；每次独立tmpfs，所有目标销毁，无主机持久新挂载。
私有mount选项和全局开关分别核验，不能把有sudo权限等同于允许修改全局配置。

## 第一门：实际首次页型
每臂一个全新64MiB文件，私有挂载192MiB容量上限，256KiB多样字节反复写，
ftruncate后直接MAP_SHARED首次写入，独立pread逐段比较，随后unmap/close/unlink。
必要创建/缓冲准备包含在create→data_ready计时，页型观测及外部验证另列。

| 单次机制观察 | never | within_size |
|---|---:|---:|
| 写入minor faults | 16384 | 32 |
| 写后实际ShmemPmdMapped | 0 | 67108864B |
| 首次写CPU | 28.686ms | 12.415ms |
| create→data_ready墙钟 | 41.461ms | 23.902ms |
| major faults / swap / pswpout增量 | 0 / 0 / 0 | 0 / 0 / 0 |

KernelPageSize/MMUPageSize仍报告4kB，因此以实际ShmemPmdMapped确认PMD映射，
不把挂载请求当实际生效，也不凭fault数量宣称同比吞吐收益。
这是两次小机制探针，无CV、不是V3吞吐或正式性能结果。

## 第二门：真实MappedFileWindow新目标成本
在[预先合同](2026-11-12-v3-private-tmpfs-54.md)固定后运行：
实际cpnetflux-mapped-cost-bench --lifecycle-page-mode ordinary --initial-state fresh，
1GiB新文件、4 workers、每路≤64MiB窗口、256KiB逻辑写粒度、profile off。
两臂均为私有tmpfs，仅huge策略不同；容量上限1280MiB；五对AB/BA/AB/BA/AB，十次全保留。
ordinary是“不调用旧collapse准备helper”，不是强制小页；真正策略由私有挂载决定。

worker_work计时覆盖实际window mapping→页型观测→首次写入→unmap，
包含目标页分配/清零成本，不含输出验证或unlink。各线程区间相互重叠，
worker墙钟和不能称完整任务墙钟。判定不使用包含删除的whole指标。

| 五次中位数 | never | within_size |
|---|---:|---:|
| worker_work CPU和 | 0.644782s | 0.323576s |
| worker_work墙钟和 | 0.656132316s | 0.323532366s |
| worker_work墙钟和population CV | 1.168599% | 1.105136% |
| worker_work minor faults | 262158 | 1553 |
| whole诊断墙钟（含验证/清理，仅辅助） | 0.324353696s | 0.161333336s |
| cleanup墙钟（单列，不用于准入） | 0.078836378s | 0.003763089s |

worker CPU降低49.8162%，worker墙钟和降低50.6910%，5/5配对改善，
两臂CV≤5%，通过事先≥15%关键工作预算门。
与旧collapse不同，收益确实存在于准备/写入/关闭的服务区间，并非仅unlink。
它仍不含网络/CRC/完整V3生命周期，不把50%局部CPU节省外推为整任务吞吐翻倍。

现有bench smaps观测位于写前，故其页型字段不能证明写后的覆盖量；
第一门独立写后smaps确认机制，第二门fault与成本提供对应工作量证据。
完整V3验证仍须采集实际接收进程页型，不能把第一门的页型覆盖率复制成1GiB受测事实。

## 内容、资源与环境门
十次均：result pass、写入与独立验证各1073741824B、4096次write、
16次map与16次unmap、seed0、major0、payload_removed true。
采样Su实际进程身份，owned swap峰0、整例host pswpout增量0，
最低MemAvailable29225926656B（≥12GiB），RSS采样峰276516864B。
RSS并不计入全部1GiB tmpfs文件容量，按各端文件预算另列。
每次目标删除/私有挂载卸载；外部挂载状态一致，无新增网络payload或临时传输服务。
原2121服务和共享/dev/shm现场终检见delivery.json。

## 下一开发与进入边界
局部准入已足够进入完整V3集成验证，不再重复局部参数/帧尺寸/并发扫描。
下一阶段实现一个显式、有界、默认关闭的私有目标后端实验驱动，
复用现有V3程序和CRC/lease/checkpoint/发布及网络/资源停止门：
1. 冻结root私有挂载、降权、marker所有权、进程/超时/取消/异常卸载与外部SHA验证合同；
   整个目标生命周期位于同一namespace，不把输出准备藏到完整任务计时外。
2. 做相关脚本/生命周期回归，固定提交、推送、双端同提交/配置/构建/SHA，
   pacing/exact-read保持off，与M1 combined的纯TCP配置一致。
3. 先限一次≤1GiB完整V3新目标可行性；记录接收实际页型、路由/绑定、刷新尾窗网络计数、
   驻留源/独立SHA/verified chunks、内存/swap与清理。
   任一网络/资源/正确性门失败即停，不通过换规模绕过。
4. ≥40Gbps且全部门过只代表准入；之后另冻同提交、相同私有挂载条件
   huge=never/within_size的完整任务对照，才讨论正式8GiB五对M2。
   当前新跨机样本0，不能承诺40或100G，也不能用本地内存写速率替代传输成绩。

原生RDMA、DEFERRED微批/策略记忆继续暂停；历史OOB因果尚未解决。
私有tmpfs实验适用于该内存文件后端，不代表磁盘/TLS/持久化/生产系统性能。

## 复核与保存
证据根/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-54：
probe-child.py/probe-private.py、never/within_size.stdout和stderr、
mechanism.json/host-state.json；
cost-private.py、十份原始stdout/stderr、cost-results.json/cost-host-state.json；
验收复算脚本和acceptance.json、最终delivery.json。
原型脚本只作为本轮实验原件保存；不是产品启动器或当前HEAD自动重跑入口。
重跑须另立scope/新目录，不覆盖这些文件。原始证据本地查看副本另存，
不将其描述为GitHub备份；本轮GitHub提交内容为明确列出的计划/结果与导航。
~~~bash
python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-54/verify.py
~~~
无运行时代码变化，复用Task53两端251项相关回归/相同二进制记录；
本次验收是原始JSON/内容/资源/隔离/清理及统计复算，不是新跑全套CTest。
tmpfs/THP机制参考[内核tmpfs文档](https://docs.kernel.org/filesystems/tmpfs.html)、
[内核THP文档](https://docs.kernel.org/admin-guide/mm/transhuge.html)，最终结论以本机证据为准。
