# Task59：项目自有缓存归属与恢复测试负载泄漏修复

日期2026-11-12，输入a1be2760d90bdecd3163d25a3c937f5c13840553。
上一轮交付C为PROGRESS，M2仍开放。本轮直接R770-1单人串行，无子代理/worktree，R2不改源码。
范围：只读盘点本项目build与CPNetFlux实验目录的实际驻留页，区分冷文件、缓存和活跃负载；
追查已发现corrupt-partial-checkpoint回归的download复制源未清理问题，补行为回归后修复。
非目标：不调系统cache/swap/THP/网卡，不碰其他项目/外部服务，不删除历史证据或旧payload，
不运行8GiB矩阵或重复旧候选，不用物理free门放宽掩盖host事件，不启动RDMA/DEFERRED。
初步受影响：tools/test/run_v3_corrupt_partial_checkpoint_smoke.py、新增相应cleanup行为测试、
CMakeLists.txt测试注册、本任务/结果及ROADMAP。若只读归属证明可安全减少自有缓存，
先补明确文件归属、范围、前后不变性/预算合同再实现，不直接执行全局清缓存。
验收：清理测试先RED后GREEN，证明成功/失败路径只删除自己创建的fixture，保留日志和manifest，
真实损坏checkpoint上传/下载语义与端点退出仍通过；相关CMake/CTest，明确提交与GitHub SHA回读，
固定代码后R2同步构建和相关回归。不得将cleanup修复直接解释为吞吐改善或正式资源解阻。
证据根：/home/Su/experiments/CPNetFlux/2026-11-12-v3-resource-reclaim-59。
只读盘点不触碰数据内容、不fault-in文件页；记录mincore前后MemFree/Available与host pswpout。

## 用户优先级变更（阶段60覆盖）

保持现有性能，转为上机前闭环。只完成本任务已发现的fixture/启动失败清理缺陷；
内存后端/项目缓存释放设计暂不实现，不继续扫描或8GiB实验。只读结果保留：
R1项目build驻留约0.88GiB、CPNetFlux历史实验文件驻留约19.64GiB（其中旧64GiB输出约13.05GiB），
说明磁盘文件大小不等于内存占用；无内容读取/清cache，host pswpout增量0。
R2扫描达到60000文件上限退出，未形成完整归属结果；不冒称双端扫描完成。
新增测试覆盖copy后的启动失败、已有源保留、源生成失败及复制源替换保护；
受影响工具须清理本次创建的数据，保留日志/manifest，不删除历史payload。
