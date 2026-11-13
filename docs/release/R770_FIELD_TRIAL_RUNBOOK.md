# CPNetFlux R770候选上机说明

范围是冻结版本CLI的受控现场验证。先读[候选事实与限制](R770_FIELD_TRIAL_CANDIDATE.md)、[冻结JSON](R770_FIELD_TRIAL_FREEZE.json)和[最新补验结果](../tasks/2026-11-13-field-trial-tls-closeout-86-result.md)。当前R770实验已结束，不要直接再次运行已完成的E80/E85/E86目录；其独占输出、租约、证书和截止时间不可复用。

## 1. 取得固定版本，保留现场

权威项目R1 `/home/Su/projects/CPNetFlux`，R2同路径；Windows只作查看、连接和证据回收。由一个写入者开发和操作Git。用最终GitHub备份回执的准确提交取得源码，两端版本一致；保留当前修改、旧程序、配置及checkpoint，禁止为换版本执行`reset --hard`或`clean`。运行源代码版本为dbb2d6b，完整测试工具输入为4170f3cc；最终文档提交包含前者，不能只检出旧运行时而遗漏后续观察器修复。

```bash
git log -1 --format='%H %s'
git status --short
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_BUILD_DESKTOP=OFF \
  -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_RDMA=ON \
  -DCPNETFLUX_ENABLE_ISAL=ON -DCPNETFLUX_ENABLE_IO_URING=ON \
  -DCPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=OFF
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 1
sha256sum build/cpnetflux-gridftp-server \
  build/cpnetflux-tree-download-client build/cpnetflux-tree-upload-client \
  build/cpnetflux-private-tmpfs-launcher
```

构建依赖以CMake实际探测为准（C++20编译器、OpenSSL、zlib、spdlog，以及所选verbs/ISA-L/liburing开发依赖）。记录编译器、内核、CMakeCache及所有失败/skip。R770原运行时完整测试各825通过/3环境skip，后续观察器4项相关CTest和本轮108项离线门另有记录；不把历史完整测试冒称新机器当前测试。相同R770构建应匹配冻结四SHA；异机重编译的二进制可能不同，须另记四SHA，双端相同后从头做功能准入，不冒用本机性能。

## 2. 先做环境和所有权清单

创建独立、权限受限的证据和测试数据根，记录路径、uid、device/inode、标记文件、启动的PID/start和端口。目标为空的新目录，业务数据、原2121服务和其他项目不进入清理范围。

记录双端地址/接口/MTU、到对端且显式source绑定的路由、NIC驱动/固件/速率、RX/TX ring、RSS/IRQ/CPU/NUMA、磁盘或tmpfs类型、挂载与剩余空间。R770管理地址192.168.1.109/.102，传输192.168.100.1/.2；新现场替换成经过核验的数据网地址。CPNetFlux控制端口49337、数据窗口61200–61711是本轮示例，须检查无占用及网络策略，不挤占已有服务。

双端分配前至少MemAvailable12 GiB、MemFree2.25 GiB；观察整个源准备、传输和清理，任何自有进程/cgroup swap、host pswpout增量、OOM/压力越界立即停。本轮正式负载最多1 GiB，先8 MiB功能；不一次生成8/64 GiB，不累计多个目标。保留基线接口/HCA计数，特别是rx_out_of_buffer、rx_missed_errors、CRC/丢弃/重传及对应队列变化。

系统网络修改必须有该现场授权。R770本次只获临时RX1024→4096、TX保持1024、最长90分钟且结束/失败即回滚的授权，已经恢复；这不授权修改另一现场的RSS、IRQ、MTU或持久化设置。若重启R770实验，须新窗口、新双端本机monotonic截止、新独立回滚监督器、观察器自进程可见性检查和队列校准，不能只手工调ring后裸跑。

## 3. 先跑小规模TLS完整树

管理员通过安全方式准备server cert/key、可信CA和相同token文件；证书SAN匹配数据地址或指定服务名，token/私钥0600、私有目录0700，均在仓库外。不使用关闭证书验证的方法替代准入。源根下的`src`放8 MiB普通文件、零长文件、空目录及嵌套空目录；目的路径使用本轮专有的新目录。

以下是从已通过TLS路径整理的命令模板，需要把`/absolute/...`、地址、证书名替换为本机已核验值。仅用于功能，未附带R770专用RSS端口计划，不是31 Gbps复现命令。先清除未声明的CPNETFLUX实验环境；完整已测环境还保存在E86的`tls-after-service-start.json`与`scenario-rows/tls-after-verified.json`。

服务端（R2）：

```bash
task_server_ip=192.168.100.2
task_source_root=/absolute/owned/source-root
task_secure=/absolute/private/credentials
env CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=off \
  CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND=off \
  CPNETFLUX_SENDER_PAYLOAD_EXECUTION=queued \
  build/cpnetflux-gridftp-server \
  --root "$task_source_root" --host "$task_server_ip" --port 49337 \
  --data-port-base 61200 --connections 4 --data-transport tcp \
  --checksum crc32c --chunk-size 1048576 --buffer-size 65536 \
  --final-verify-policy verified_chunks --commit-sync-policy none \
  --auth-mode token --auth-token-file "$task_secure/token" \
  --tls-mode required --data-tls-mode required \
  --tls-cert-file "$task_secure/server.crt" --tls-key-file "$task_secure/server.key"
```

客户端（R1）：

```bash
task_client_ip=192.168.100.1
task_server_ip=192.168.100.2
task_server_name=192.168.100.2
task_target=/absolute/owned/new-target
task_evidence=/absolute/owned/evidence
task_secure=/absolute/private/credentials
env CPNETFLUX_STAGE_READ_CONCURRENCY=0 CPNETFLUX_STAGE_WRITE_CONCURRENCY=0 \
  CPNETFLUX_STAGE_BUFFER_BYTES=67108864 CPNETFLUX_PAYLOAD_PIPELINE_DEPTH=2 \
  CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION=queued \
  CPNETFLUX_SENDER_PAYLOAD_EXECUTION=queued \
  CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE=pwrite \
  CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES=off CPNETFLUX_TCP_EXACT_READ=off \
  CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=off \
  CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND=off \
  build/cpnetflux-tree-download-client \
  --host "$task_server_ip" --port 49337 --local-bind-address "$task_client_ip" \
  --source-dir src --dest-dir "$task_target" --connections 4 --file-parallelism 4 \
  --control-reuse worker --data-session-reuse tree --file-scheduling dynamic \
  --data-pending-window 2 --data-transport tcp --compression off \
  --checksum crc32c --chunk-size 1048576 --buffer-size 65536 --data-frame-bytes 65532 \
  --final-verify-policy verified_chunks --commit-sync-policy none \
  --manifest-flush-policy every_n_chunks --manifest-flush-interval-chunks 16 \
  --range-checkpoint-batch-chunks 16 --posix-write-strategy direct --file-io-buffer-size 0 \
  --reconnect-attempts 2 --transfer-timeout-ms 10000 \
  --auth-mode token --auth-token-file "$task_secure/token" \
  --tls-mode required --data-tls-mode required --tls-ca-file "$task_secure/ca.crt" \
  --tls-server-name "$task_server_name" --phase-timing on \
  --json-summary "$task_evidence/tls-smoke.json"
```

双端各自生成外部文件清单：相对路径、类型、大小、完整SHA-256及全部目录集合；比较源/目标，不能只看客户端result=pass或树哈希。JSON应为`actual_transport=v3`、`data_transport=tcp`、`control_tls_verified=true`、`data_tls_verified=true`、文件/目录全部完成且failed计数0；端点确实绑定数据网。换错token应明确拒绝且无发布文件，不能把超时当认证拒绝。

停止本轮拥有的服务并等待进程退出，记录PID/start；用相同二进制、配置和凭据策略重启（新PID/start），用新目标/新证据标签重复完整树核验。再对本轮临时数据做取消、断连、服务重启后的恢复，核对verified chunks、重试预算、最终SHA及完整目录；不在业务数据上注入坏数据/checkpoint。阶段86重启TLS已通过，阶段85六项故障为单文件；新现场仍需自己的核验结果。

## 4. 功能通过后再做受控1 GiB内存任务

需要复现明文性能时，采用冻结的A配置而非上述TLS模板：四TCP、1 MiB chunk、256 KiB有效DATA、CRC32C/verified_chunks、checkpoint16、压缩/TLS off、应用pacer每lane1.5e9 B/s、kernel pacer off、驻留tmpfs源、原生监督的私有huge=within_size tmpfs目标和inline mmap接收。保持有界内存/命名空间/所有权；不要修改全局tmpfs/THP/cache/swap策略。

实际完整调用/环境/计时合同在E85 `formal/C-1-cp.json`、`rows/C-1-cp`与E86 `formal/admission-cp.json`；重建新现场的本机时钟域、RSS/端口计划和CPU/NAPI校准后才能使用。`tools/perf/private_tmpfs.py`及`run_private_tmpfs_v3.py`是R770私有实验驱动，含专用路径/源布局/资源合同；不能当任意机器一条命令启动的安装器。仅替换IP或复制旧plan会丢失必要门禁。

先一次1 GiB准入，完整性/资源/NIC/清理全部通过后再决定重复；不扫描多个速率、帧或队列参数掩盖失败。要做新GridFTP比较，先刷新GSI proxy并验证有效期、两端服务和空间；使用固定配置的交错配对，完整任务计时。分别报告认证、加密、checksum、后端页型和提交语义差异，失败/认证错误不计性能。

## 5. 后续现场验收顺序

1. 完整树、正反向、空/零文件、错误token和恢复先过。
2. 验证1 GiB明文内存准入；如需比较，独立固定五对及dense/tiny/mixed/反向，不与旧失败矩阵拼样本。
3. 重复任务测试保留同一常驻服务身份、FD/线程/RSS趋势，至少30分钟且100个任务；若要声称持续高负载，应另做明确连续网络忙碌时间的测试，记录busy时间/占空比，不能以SHA和清理间隔充当发送时间。
4. 物理磁盘测试单列：先测两端读写上限，再选明确的持久化提交策略并计入sync/发布成本。内存31 Gbps不代表落盘成绩；本轮`commit-sync none`不验证掉电。
5. 大文件TLS吞吐单列，使用合法TLS配置；原生verbs、WAN和多链路另立合同，不由本轮TCP结果外推。

## 6. 停止门与回退

任一内容/目录/终态/身份/时限/换页/NIC硬门失败即停止该窗口，不修后同窗重试、不删除失败证据。失败stdout吞吐可能采用计划字节，必须排除；保留实际字节、结构化错误、manifest、接口前后快照和首要失败原因。

按PID和start_ticks匹配，只停止本轮自有客户端、服务、代理、观察器与其后代并等待退出。先释放fd/mmap，再卸载本轮私有挂载；按预先记录的标记/uid/dev/inode删除本轮payload和partial，保留诊断、清单、日志和哈希。私钥、token、proxy不归档。核对无owned监听、挂载、残留payload，双端host pswpout无新增，原服务身份和网络配置相同；若临时调过ring立即恢复原RX/TX且不持久化。

程序回退用已备份的明确提交/上一版程序与匹配状态，先保留现场修改。schema2不能假设旧程序可直接读取；不要用代码回退清理实验目录或业务数据。记录结束状态、准确提交、四SHA、参数、开始/结束时间、成功/失败任务、资源趋势及证据归档SHA，形成下一次交接回执。
