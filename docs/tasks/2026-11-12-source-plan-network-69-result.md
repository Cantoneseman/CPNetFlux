# 阶段69：A失败并串行交还

输入提交 f4129e88ab0f902cd4d0c3dba63f5f2a14b1ebed，运行时代码42718ac26be24d53ec17525a7b1b3bcb1e340931。双端同配置、四二进制SHA与交接冻结值一致，项目源码、Git index和既有两份untracked均未修改。

## 实际执行与结论

仅运行一次1GiB、4TCP、R2驻留源到R1新私有tmpfs目标。文件任务result=pass、completed_files=1、failed_files=0、实际传输1073741824B，1024个chunk检查点校验通过，外部sha256sum两端一致：d7c278657703dde98c76d9c6d53c5d3413beace312d54502266a42a991ed4cf3。

**阶段A FAIL_NETWORK，性能有效样本0。** R1 rx_out_of_buffer +132；rx_missed_errors wire窄窗+94、0.5s刷新后+132，宽窗口同为+132。之后最终刷新计数无继续增量。R2相应错误0；双端HCA错误无增量。没有后续负载或重跑挑样。阶段B正式配对NOT_RUN/NO_GO，C稳定性NOT_RUN，8GiB和64GiB NOT_RUN，field NOT_READY。

## 端口计划与原生启动

沿用固定native私有tmpfs启动器（其SHA未改变），使用独立实验目录sitecustomize注入管理源路径、server data_base=61200及client唯一新增--tcp-data-source-port-plan完整路径；未修改tracked脚本。启动方式差异、精确argv和PID见native-command.json、A-attempt.json、A-driver-process.json、management-audit.jsonl、A-row/server.json。产品自身端点回执独立证实实际使用计划：

|channel|R2发送端口|R1接收端口|RX queue|CPU|NAPI|
|---|---:|---:|---:|---:|---:|
|0|61202|20054|9|9|8202|
|1|61200|30278|27|27|8220|
|2|61201|40538|7|7|8200|
|3|61203|50754|28|28|8221|

每条bind_attempts=1。按实际四元组独立Toeplitz重算与CPU/NAPI一致，RSS/fields实测前后指纹未变：0782e84498ad0ce26c45f313d7ca4fc81858ac1a9e2a93afce8c1e68663a19e5。四队列RX包数约185730/186289/186324/186330，已经分散，仍发生OOB。**四队列分流在本配置下不足以消除错误**，不能宣称解决；接口聚合计数不能定位具体队列或证明分配失败。相关queue buff_alloc_err、wqe_err、pp_alloc_slow及softnet dropped/time_squeeze本窗增量0；不据此排除其他接收路径机制。

## 原始时序与边界

应用summary：0.236867304s、36.264754Gbps。client进程启动至退出：0.243608965s、35.261160Gbps。native目标setup 0.010733808s + mkdir0.000301373s + client：0.254644146s、33.733093Gbps。以上均为失败样本的诊断数字，不能作为验收/稳定吞吐。没有payload稳态区间测量，应用summary不能称稳态。

实时RSS/fields/plan预检在目标任务计时前执行，未单独计时，因此33.733093不代表包含全部必要预检成本的严格端到端速率；该计时缺口如实保留，不补造数值。client内计划读取/解析在进程计时中。计划冷生成64.72s为已冻结历史配置成本单列。源准备、源/目标外部SHA、观测和清理单列。源为stock admission随机8MiB块重复128次且完全物化；阶段64wrapper为固定伪随机块，本次未声称源bitwise相同。两者均压缩off。

## 资源、清理与局限

native_syscalls、huge=within_size；目标实际ShmemPmdMapped采样峰262144000B，非全文件大页覆盖声明。双端root observer实际覆盖173/162次采样，包含client5次、server74次；采样含/proc扫描耗时，不是严格50Hz。host pswpout增量0、owned process/cgroup swap峰0；MemAvailable最低R1 29398179840B、R2 29040353280B。外部client RSS采样峰217604096B，client内部采样218361856B；server14721024B。实际memory.max/high为max，无有限cgroup硬上限；应用payload/map预算由原固定harness检查，不能把其当cgroup限额。

源准备早于新outer网络watch，保留原seed-resource与初始化证据；并非从seed前完整HCA监测。准备失败、R1自SSH别名缺失等列于preparation-history.json；都发生于本次唯一发载前。

清理已核验：两端69私有payload roots不存在，无69临时mount、49337监听、owned活动数据/观察进程；原2121服务PID/startticks保持。停止/清理按标记和确切拥有路径由固定harness执行，无reset/clean，无全局网络/内存调优，无旧证据或CPSS修改。R2完整小体量实验记录已收集在r2-evidence。

离线29项一致性审计通过，含实际路径、SHA、四队列、资源采样、Git状态和预期失败门；这不等于网络验收通过。audit69.py可重复只读审计。

## 交还root的下一步

A失败即停止，不能进入GridFTP正式比较、TLS/加密组或稳定性。root可据本证据设计有界、可证伪的接收服务/突发诊断，不能把RSS分散成功当作OOB根因已解决。下一轮如修代码由root串行接回，不在本实验入口改生产逻辑。

实验聊天仅写实验文件并串行交还root；root已复核98项冻结hash，报告及紧凑证据清单由本次文档提交备份。完整98项原始证据仍在服务器实验目录及Windows校验副本；不把它们全部称为GitHub备份。最终提交/远端SHA在E70/backup.json回执中，避免自引用。

root后续：见[阶段70：任务级接收CPU隔离诊断](2026-11-12-rx-cpu-isolation-70.md)。69的网络FAIL及B/C NOT_RUN不改。相关运行时沿用阶段68双端各156项回归；只改文档/实验入口，不重跑无关全套。
