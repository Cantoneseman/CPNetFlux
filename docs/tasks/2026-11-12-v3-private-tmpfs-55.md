# Task55：私有tmpfs完整V3有界准入
输入：dbe21c4c6a14c5c59e336f9aec01934ae853eb9e；运行时79b49022。
范围：将Task54已通过的首次大页机制接入默认关闭的实验启动器；使用现有V3，
固定1GiB新目标、4 TCP、256KiB DATA、CRC32C/verified_chunks、checkpoint16、
inline mmap/64MiB窗口、1MiB接收buffer、TLS/压缩/pacing/exact-read关闭。
非目标：不修改生产运行时/协议/默认值、全局THP/共享挂载/网络参数；
不恢复RDMA、DEFERRED或旧失败候选；不做参数扫描。
允许修改：tools/perf/private_tmpfs.py、tools/perf/run_private_tmpfs_v3.py、
tools/test/run_private_tmpfs_test.py、tools/perf/run_small_file_baseline.py（仅单列目标目录准备时间）、
CMakeLists.txt（测试注册）、本合同/对应结果、docs/README.md及docs/ROADMAP.md。
验收：先行为测试（默认禁用、路径/权限/namespace隔离、超时/取消/失败清理、门限），
再真实私有namespace64MiB正常/失败/超时/取消生命周期smoke，以及128MiB本机V3 mmap闭环（源+目标≤256MiB）。
固定提交推送、R2同步构建、相关CTest、配置/二进制hash相同才允许唯一一次1GiB跨机样本。
显式private mount size=1280MiB、huge=within_size；挂载由root完成，
实际payload/SSH/SHA降权Su；全生命周期/SHA在该namespace内。finally终止子进程后卸载，
只删除外部inode/owner核验的自有目录；日志保留，禁止删除无关数据。
采样实际接收目标VMA的ShmemPmdMapped；不得用请求策略当实际页型。
内存>=12GiB、owned swap0、host pswpout增量0、源全驻留、独立SHA、1024 chunks、
窄窗/刷新尾窗/整体网络OOB和missed0、明确route/bind、原2121身份不变。
目标必要挂载与目录准备加至客户端完整任务wall，另列历史口径客户端wall；
源驻留准备、外部观测、独立SHA、清理另列，不混入吞吐收益。
任一门失败即停，不通过改规模/并发绕过；>=40Gbps且全门通过仅准入，
之后另冻结同提交同私有挂载never/within_size的8GiB五对合同。
本轮工程结束不等于M2或100G达标。证据目录：
/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-55。
