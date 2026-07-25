# GridFlux 项目状态记录

## 2026-07-22 Lab Beta 5 总结证据包与汇报材料收口（CPSS-aware go review-ready）

### 当前口径

- Beta 5 retained evidence：`tools/perf/results/20260722T080536Z_lab-beta-5-report-evidence-pack/`。
- 本轮只汇总 Beta 1/2/3/4 已有 retained evidence，生成 PPT/论文可用 CSV/JSON 和中文报告；不改写旧 retained evidence。
- Beta 1：公网协议基线 `full_pass/go`，真实跑通 GridFlux、FTP、GridFTP。
- Beta 2：公网连接复用扩展 `pass/go`，worker control reuse 在小文件密集型场景显著减少控制连接并带来加速。
- Beta 3：修复后 compression staging rerun 仍保留为 `partial_go_cpss_environment_blocked`；raw/gzip/lz4 在 GridFlux、FTP、GridFTP 下 transfer 与 restore/tree hash 正确性通过。
- Beta 4：CPSS environment unblock and minimal staging gate 为 `cpss_pass_go`；GridFlux CPSS worker 与 GridFTP CPSS 均 3 datasets × 3 repeats 通过，FTP CPSS 为 optional `environment_blocked`，不作为核心失败。
- 组合口径：Beta 1/2/3/4 evidence 支持进入 CPSS-aware go 评审。

### 边界

- 压缩仍是 staging/restore 编排层，不在 GridFlux C++ 数据热路径，不改变公开协议/API。
- 未跑 10GiB/20GiB/100GiB/heavy soak，不能宣称 50G/100G ready。
- CPSS 收益依赖数据可压缩性；HPC science 样本压缩率接近 1，AI mixed 和 metadata 类数据更适合展示压缩 staging 收益。
- `scp`/`rsync` 仅用于源码/文档同步和 evidence 管理，不作为公网传输结论替代。

## 2026-07-22 Lab Beta 3 文档收口（raw/gzip/lz4 staging correctness passed，CPSS environment_blocked）

### 当前口径

- 原 Beta 3 retained evidence 保留为历史 no-go：`tools/perf/results/20260720T051105Z_lab-beta-3-public-compression-staging-matrix/`，结论 `fail_correctness` / no-go。
- failure triage evidence：`tools/perf/results/20260721T034707Z_lab-beta-3-failure-triage-gridflux-lz4-final-status/`，结论 `pass_after_fix`；确认 `oss_ai_training_mixed` / `gridflux_lz4_worker` 的尾部 data final status timeout 误判已通过最小公网复现修复验证。
- rerun after fix evidence：`tools/perf/results/20260721T093510Z_lab-beta-3-compression-staging-rerun-after-final-status-fix/`，结论 `partial_go_cpss_environment_blocked`。
- 当前 Beta 3 口径：raw/gzip/lz4 Beta 3 staging correctness passed；GridFlux、FTP、GridFTP 的 raw/gzip/lz4 transfer status 与 restore/tree hash 均通过。
- CPSS 仍为 `environment_blocked`，不是 `full_go`；不得把 CPSS 写成已通过。
- 旧失败点 `oss_ai_training_mixed` / `gridflux_lz4_worker` repeat 0..2 已在 rerun 中全部通过；repeat 2 命中 `recovered_after_data_final_status_timeout=true`，但 client exit、summary、bytes/files 和 restore/tree hash 均一致。
- 两端 fresh CTest 已验收为 `185/185` pass。

### 边界

- 本轮只做文档/索引收口，不跑新矩阵。
- 未跑 10GiB/20GiB/100GiB/heavy soak。
- 不修 CPSS，不做 CPSS 训练或大缓存构建。
- 未进入 GridFlux C++ hot-path 压缩设计；gzip/lz4/CPSS 仍只属于 staging/restore 层。
- 不改写三个旧 retained evidence 目录下的历史结果文件。

## 2026-07-20 Lab Beta 3 公网压缩 staging 矩阵（已完成，no-go）

### 本轮结果

- 新增 retained runner：`tools/perf/results/20260720T051105Z_lab-beta-3-public-compression-staging-matrix/beta3_public_compression_staging_runner.py`。
- 使用深圳公网 `120.25.121.51` -> 上海公网 `47.116.174.181`，只跑 OSS 科学数据集上的 compression staging 矩阵。
- 数据源优先使用深圳已配置的 OSS bucket `science-compressor-datasets`，总量硬上限 `<= 3GiB`，默认目标约 `1.2-1.6GiB`。
- 压缩只在 staging/restore 层进行，不进入 GridFlux C++ protocol hot path，不改变公开 API。
- Evidence 目录：`tools/perf/results/20260720T051105Z_lab-beta-3-public-compression-staging-matrix/`。
- 结论：`fail_correctness` / no-go。矩阵产出完整，`method_runs.jsonl` 为 99 行；raw/gzip/lz4 的 FTP 与 GridFTP anchor 均通过，CPSS 因临时 venv/install check 失败按计划标为非阻塞 environment-blocked。
- 唯一必需路径失败项为 `oss_ai_training_mixed` 的 `gridflux_lz4_worker` repeat 2：GridFlux client 返回 `exit_code=1`，summary 为 `result=fail`、`error_code=io_error`、`recv: Resource temporarily unavailable`。远端 restore 后 tree hash/bytes/files 与源一致，但传输命令本身未成功，不能进入更大数据或协议内压缩设计。

### Beta 0/1/2 验收状态

- Lab Beta 0：ctest gate 修复、公网小烟测和清理证据已完成，结论 `pass/go`。
- Lab Beta 1：公网协议基线对比已完成，FTP/GridFTP/GridFlux anchor 跑通，结论 `full_pass/go`。
- Lab Beta 2：公网连接复用扩展矩阵已完成，GridFlux worker 复用收益与正确性门禁通过，结论 `pass/go`。
- 最新双端 targeted/full CTest 已验收为通过；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 允许因环境不可用 skip，不允许 failed tests。

### 本轮边界

- 不跑 10GiB/20GiB/100GiB/heavy soak。
- 不跑 CPSS 训练、大缓存构建或压缩正式矩阵。
- 不使用 SSH tunnel、私网 IP、scp/rsync 替代 FTP/GridFTP/GridFlux 协议传输。
- 不记录 OSS 凭据、token、password 或其他 secret。

## 2026-05-19 Phase 6E 完整 alpha 原型收口与长跑验收包（已完成）

### 实现目标

- 新增 `tools/release/run_alpha_release_candidate.py`，一键编排 full alpha gate、long soak、public hygiene、artifact manifest freshness 和 remote artifact sync/verify。
- 扩展 `tools/test/run_alpha_soak_smoke.py`，支持 `--duration-seconds`、`--profile tiny|small|mixed`、`--token`、`--tls` 和 `--data-tls`。
- 新增最终 alpha 限制清单 `docs/release/ALPHA_LIMITATIONS.md` 和 alpha 架构说明 `docs/ARCHITECTURE_ALPHA.md`。
- 新增 Phase 6E 报告入口 `docs/release/PHASE6E_ALPHA_RC.md`。

### 默认值与边界

- 默认仍为 `auth-mode=anonymous`、`tls-mode=off`、`data-tls-mode=off`、`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。
- Phase 6E 不新增 GSI、AUTH TLS、raw FTP stream、raw FTP recursive transfer、生产认证、集中式监控或 100G 性能专项。
- Token、证书私钥和私有凭据仍只作为 runtime 输入，不进入 public docs、artifact manifest 内容、JSON summary 或 event log。

### 已执行验证

- 脚本编译：`python3 -m py_compile tools/release/run_alpha_release_candidate.py tools/release/run_alpha_release_gate.py tools/release/test_alpha_release_helpers.py tools/test/run_alpha_soak_smoke.py tools/test/test_alpha_soak_smoke.py tools/demo/run_alpha_demo.py tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/release/sync_remote_artifacts.py tools/release/check_remote_artifact_sync.py` 通过。
- Release/helper 测试：`python3 tools/release/test_alpha_release_helpers.py` 通过；`python3 tools/test/test_alpha_soak_smoke.py` 通过。
- 本机 Debug full CTest：`181/181 passed`。
- 本机 `build-io-uring-real` Release full CTest：`181/181 passed`，`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`。
- <redacted>二 Debug full CTest：`181/181 passed`。
- <redacted>二 `build-io-uring-real` Release full CTest：`181/181 passed`，`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`。
- Public export strict hygiene：`pass`，`/tmp/gridflux-public` 检查通过。
- Quick alpha gate：`pass`，JSON 为 `tools/perf/results/20260519T023443Z_alpha-release-gate.json`。
- Full alpha gate：`pass`，JSON 为 `tools/perf/results/20260519T023747Z_alpha-release-gate.json`，artifact manifest 为 `tools/perf/results/20260519T023747Z_alpha-artifacts.json`；freshness `checked=872 stale=0 status=pass`，sync/verify `missing=0 mismatch=0 status=pass`。
- Alpha release candidate：`pass`，报告为 `docs/release/ALPHA_RELEASE_CANDIDATE.md`，JSON 为 `tools/perf/results/20260519T030518Z_alpha-release-candidate.json`，artifact manifest 为 `tools/perf/results/20260519T030518Z_alpha-release-candidate-artifacts.json`。
- RC long soak：`iterations=5`、`pass_count=5`、`fail_count=0`、`total_bytes=96584080`，event log 为 `tools/perf/results/20260519T030518Z_alpha-release-candidate/alpha_long_soak_events.jsonl`。
- RC artifact freshness：`checked=1080 stale=0 status=pass`；artifact sync：`checked=1081 synced=123 missing=0 mismatch=0 status=pass`；artifact verify：`checked=1081 missing=0 mismatch=0 status=pass`。
- 修复 RC 收口过程中暴露的 release artifact 规则：`.jsonl` event log 现在作为安全文本 artifact 被允许并分类为 `event_log`；RC 最终 manifest 生成后成功路径不再改写 RC JSON/Markdown，避免 manifest hash 再次陈旧。

### 收口状态

- Phase 6E 交付包已经具备一键 RC 验收入口、long soak、最终限制清单、alpha 架构文档、RC 报告、artifact freshness 和远端 sync/verify 闭环。
- 默认传输策略未改变。
- 仍不是 beta/production：限制见 `docs/release/ALPHA_LIMITATIONS.md`。

## 2026-05-19 Phase 6D 数据通道 TLS alpha（已完成）

### 实现内容

- 新增 `--data-tls-mode off|required`，默认 `off`。
- `gridflux-gridftp-server --data-tls-mode required` 必须与 `--tls-mode required` 同用，复用 control TLS cert/key。
- `gridflux-file-client`、`gridflux-file-download-client` 和 tree upload/download clients 可通过 `--data-tls-mode required --tls-ca-file <path>` 对 STOR/RETR framed file data socket 做 TLS handshake。
- STOR/RETR frame、CRC32C、manifest、resume、verified_chunks 和 final verify 语义未改变。
- LIST/NLST ASCII listing data channel 明确不在 Phase 6D 保护范围内，仍保持现有明文 metadata data 行为。
- 新增 `data_tls_required` / `data_tls_failed` 错误码分类和 release/demo summary 分类。
- 新增本机 `gridflux_gridftp_data_tls_smoke`，覆盖 STOR/RETR data TLS、明文 data client 失败、tree upload/download data TLS 以及 LIST/NLST 明文 listing 回归。
- Release gate quick 接入 local data TLS smoke，full 接入 private STOR/RETR data TLS smoke。

### 已执行验证

- 通过：`cmake --build build -j2`
- 通过：`ctest --test-dir build -R "DataTls|data_tls|tls|ControlOptions|FileTransferOptions|FileDownloadOptions|TreeTransferOptions|EventLog" --output-on-failure`
- 子集结果：35/35 passed。
- 通过：`python3 tools/test/run_gridftp_data_tls_smoke.py --build-dir build --bytes 65536`

### 收口验证

- 本机 Debug full CTest：`180/180 passed`。
- 本机 `build-io-uring-real` Release full CTest：`180/180 passed`，真实 io_uring smoke `Passed`。
- <redacted>二 Debug full CTest：`180/180 passed`。
- <redacted>二 `build-io-uring-real` Release full CTest：`180/180 passed`，真实 io_uring smoke `Passed`。
- Quick alpha gate：`pass`。
- Full alpha gate：`pass`，artifact sync/final verify `checked=819`、`missing=0`、`mismatch=0`、`status=pass`。
- Public export strict hygiene：`pass`。
- 最终残留进程检查：两台<redacted>无 `gridflux-gridftp-server` / `gridflux-file-*`。

### 默认值与边界

- 默认仍为 `auth-mode=anonymous`、`tls-mode=off`、`data-tls-mode=off`、`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。
- 不实现 GSI、AUTH TLS、raw FTP TLS compatibility、生产证书管理、raw FTP STOR/RETR 或 LIST/NLST data TLS。

## 2026-05-15 Phase 0 初始化

### 已阅读文档

- `INDEX.md`
- `AGENTS.md`
- `docs/DESIGN.md`
- `docs/ROADMAP.md`
- `docs/ENGINEERING.md`

### 执行范围

- 执行 `ROADMAP.md` 中的 Phase 0：初始化 C++20/CMake 工程骨架。
- 按用户后续要求，将项目 CMake 最低版本设置为 `3.20`。
- 按 `ROADMAP.md` 文档更新规则，将 `INDEX.md` 与 `docs/ROADMAP.md` 的当前状态更新为 Phase 0 已完成。
- 未实现 Phase 1 网络传输逻辑。
- 未添加 socket、epoll、多流、客户端/服务端传输代码。

### 本机工具链

- 系统：Ubuntu 22.04.5 LTS，Linux 5.15。
- CMake：3.22.1，项目最低要求为 3.20。
- GCC/G++：13.4.0，来自 `ppa:ubuntu-toolchain-r/test`。
- clang-format：14.0.0。
- clang-tidy：14.0.0。
- Ninja：1.10.1。
- sshpass：1.09。
- rsync：3.2.7。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：2/2 passed。

### <redacted>二同步与验证

- 通过：使用 `sshpass + rsync` 同步到 `root@<redacted>:/root/projects/GridFlux/`。
- 系统：Ubuntu 22.04.5 LTS，Linux 5.15。
- CMake：3.22.1，项目最低要求为 3.20。
- GCC/G++：13.4.0，来自 `ppa:ubuntu-toolchain-r/test`。
- clang-format：14.0.0。
- clang-tidy：14.0.0。
- Ninja：1.10.1。
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：2/2 passed。

## 2026-05-15 GridFTP 源码学习经验整合

### 已阅读文档

- `docs/GridFTP传输源码学习笔记.md`

### 文档更新

- 更新 `docs/DESIGN.md`：补充 GridFTP 源码经验取舍、架构映射、内部 offset-aware chunk frame 协议、CMake 3.20+ 选型和 Mode E 风险规避。
- 更新 `docs/ROADMAP.md`：将 Phase 1/2/3 任务补充为多连接 `ConnectionContext`、固定 chunk frame、manifest/range 恢复和 GridFTP 兼容边界。
- 更新 `docs/ENGINEERING.md`：补充传输边界、连接状态、数据帧约束、range/manifest 测试要求和第一版范围控制。
- 更新 `AGENTS.md`：同步 CMake 3.20+ 工具链要求。

### 验证

- 本次为文档整合，未修改 C++ 源码和 CMake 构建逻辑。
- 未运行构建测试。

## 2026-05-15 Phase 1.0 最小网络闭环

### 实现内容

- 新增 `gridflux-server` 与 `gridflux-client` 两个可执行入口。
- 使用 POSIX socket + epoll 实现多连接 TCP sink。
- 支持 memory-to-memory 吞吐测试，server 接收并丢弃数据，client 重复发送预分配内存 buffer。
- 新增 `ConnectionContext`，集中管理 fd、连接状态、EOF、错误号、接收/发送字节数。
- 新增 `ThroughputCounter` 和最小 `Status` / `Result<T>`。
- 新增 sink 参数解析，支持 `--host`、`--port`、`--connections`、`--bytes`、`--buffer-size`。
- 新增 `.gitignore`，忽略 `/build/`。

### 未实现内容

- 未实现 GridFTP 控制面。
- 未实现 io_uring。
- 未实现断点续传、manifest、checksum、文件传输。
- 未实现 offset-aware chunk frame；Phase 1.0 只做 TCP sink 和内存吞吐闭环。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：15/15 passed。
- 通过本机 loopback smoke test：

```bash
./build/gridflux-server --host 127.0.0.1 --port 19000 --connections 4 --bytes 67108864 --buffer-size 65536 > /tmp/gridflux-server.log 2>&1 &
server_pid=$!
sleep 1
./build/gridflux-client --host 127.0.0.1 --port 19000 --connections 4 --bytes 67108864 --buffer-size 65536
wait "$server_pid"
cat /tmp/gridflux-server.log
```

输出摘要：

```text
client sent_bytes=67108864 elapsed_seconds=0.00752221 throughput_gbps=71.3714
server received_bytes=67108864 elapsed_seconds=0.0075007 throughput_gbps=71.5761
```

### 后续私网测试命令

<redacted>一 `<redacted>`：

```bash
./build/gridflux-server --host <redacted> --port 19000 --connections 8 --bytes 1073741824 --buffer-size 65536
```

<redacted>二 `<redacted>`：

```bash
./build/gridflux-client --host <redacted> --port 19000 --connections 8 --bytes 1073741824 --buffer-size 65536
```

### 备注

- 曾误对 `CMakeLists.txt` 执行 `clang-format` 导致 CMake 语法损坏，已恢复。后续只对 C++ 源码和头文件执行 clang-format。
- `clang-tidy` 手动检查产生大量风格类提示，并因直接调用方式未完整复用 CMake 编译上下文而报错；本次以 configure/build/ctest 和 loopback smoke test 作为验收结果。

## 2026-05-15 Phase 1.1 裸性能基线工具

### 实现内容

- 新增 `tools/perf/collect_env.sh`，采集 OS/kernel、CPU、内存、IP、TCP sysctl，以及可用时的 `iperf3`、`fio`、`numactl`、`ethtool` 信息。
- 新增 `tools/perf/run_loopback_matrix.py`，支持 loopback smoke/full matrix，并输出 CSV。
- 新增 `tools/perf/sync_remote.sh`，通过 `rsync -az --delete` 同步代码并排除构建产物和 `_deps/`，密码只通过 `GRIDFLUX_SSH_PASSWORD` 读取。
- 新增 `tools/perf/run_private_once.sh`，准备<redacted>一 server + <redacted>二 client 的私网单次测试。
- 新增 `docs/perf/README.md`，记录环境采集、loopback matrix、远程同步、远程构建、私网单次测试命令。
- 更新 `.gitignore`，忽略 `/build/`、`/build-verify/`、Python 缓存和性能测试临时日志。

### 未实现内容

- 未实现文件传输。
- 未实现 chunk frame。
- 未实现 manifest、断点续传、checksum。
- 未实现 GridFTP 控制面。
- 未引入 io_uring。
- 未做系统级调参，只采集当前环境参数。

### 本机验证

- 通过：`bash -n tools/perf/collect_env.sh tools/perf/sync_remote.sh tools/perf/run_private_once.sh`
- 通过：`python3 -m py_compile tools/perf/run_loopback_matrix.py`
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：15/15 passed。

### 已生成结果

- 环境快照：`tools/perf/results/env-local.md`
- Loopback smoke matrix CSV：`tools/perf/results/20260515T143350Z_loopback-smoke.csv`
- Smoke matrix 覆盖两组参数：
  - `connections=1, buffer_size=65536, bytes=8388608`
  - `connections=4, buffer_size=262144, bytes=8388608`
- 两组结果均为 `pass`。

### 待人工确认后运行

- 完整 loopback matrix：

```bash
tools/perf/run_loopback_matrix.py --build-dir build --bytes 1073741824 --output-dir tools/perf/results
```

- <redacted>二同步：

```bash
export GRIDFLUX_SSH_PASSWORD='***'
tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
```

- <redacted>二构建：

```bash
ssh root@<redacted> 'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
```

- 私网单次测试：

```bash
tools/perf/run_private_once.sh --remote root@<redacted> --server-host <redacted> --build-dir /root/projects/GridFlux/build --connections 8 --bytes 1073741824 --buffer-size 65536
```

## 2026-05-16 Phase 1.2A 最小文件传输闭环

### 实现内容

- 新增 `gridflux-file-server` 与 `gridflux-file-client`，保留既有 `gridflux-server` / `gridflux-client` memory sink 行为不变。
- 新增固定 64 字节二进制 `FrameHeader`，字段包含 magic、version、header size、type、flags、stream id、chunk id、offset、payload size、total size；所有整数按 network byte order 编解码。
- 新增 frame encode/decode/validate，拒绝非法 magic/version/header size/type、非零 flags、payload 超过 buffer size、payload range 越界和 total size 不一致。
- 新增 `ChunkRange` 与静态 chunk planner，按文件大小和 chunk size 切分，按 `chunk_id % connections` 分配 stream。
- 新增 POSIX file wrapper，使用 `pread` / `pwrite` 处理短读、短写和 `EINTR`。
- 新增 file transfer 参数解析，server 支持 `--host`、`--port`、`--output`、`--connections`、`--buffer-size`，client 支持 `--host`、`--port`、`--input`、`--connections`、`--chunk-size`、`--buffer-size`。
- Server 使用 epoll 接收多连接 frame，按 offset 写入目标文件；client 采用每连接一个发送线程的静态分片模型。
- 新增单元测试覆盖 frame 编解码、chunk planner 边界、file transfer 参数解析、POSIX file read/write。

### 未实现内容

- 未实现 GridFTP 控制面。
- 未实现 manifest、断点续传、checksum pipeline、ACK/重传。
- 未实现 QUIC、FEC、RDMA、io_uring。
- 未做系统级性能调参。
- 未修改 Phase 1.1 性能脚本的数据面行为。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：33/33 passed。
- 通过既有 memory sink 回归 smoke test：`gridflux-server` / `gridflux-client`，2 connections，1MiB。
- 通过本机 16MiB / 4 connections 文件传输 loopback smoke test：

```bash
dd if=/dev/urandom of=/tmp/gridflux-src-16m.bin bs=1M count=16 status=none
rm -f /tmp/gridflux-dst-16m.bin /tmp/gridflux-file-server.log

./build/gridflux-file-server \
  --host 127.0.0.1 \
  --port 19300 \
  --output /tmp/gridflux-dst-16m.bin \
  --connections 4 \
  --buffer-size 65536 \
  > /tmp/gridflux-file-server.log 2>&1 &
server_pid=$!

sleep 1

./build/gridflux-file-client \
  --host 127.0.0.1 \
  --port 19300 \
  --input /tmp/gridflux-src-16m.bin \
  --connections 4 \
  --chunk-size 1048576 \
  --buffer-size 65536

wait "$server_pid"
cmp /tmp/gridflux-src-16m.bin /tmp/gridflux-dst-16m.bin
cat /tmp/gridflux-file-server.log
```

输出摘要：

```text
file_client sent_bytes=16777216 elapsed_seconds=0.00845399 throughput_gbps=15.8763
file_server received_bytes=16777216 elapsed_seconds=0.00852213 throughput_gbps=15.7493
cmp_status=0
```

- 通过 0 字节文件 loopback smoke test，2 connections，FIN-only 路径成功，`cmp_status=0`。

### 后续私网手动验收命令

### <redacted>二同步与验证

- 通过：使用 `tools/perf/sync_remote.sh` 同步到 `root@<redacted>:/root/projects/GridFlux/`。
- 同步时 `rsync` 提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：33/33 passed。

### 私网手动验收

<redacted>一 `<redacted>`：

```bash
rm -f /tmp/gridflux-dst-64m.bin
./build/gridflux-file-server \
  --host <redacted> \
  --port 19310 \
  --output /tmp/gridflux-dst-64m.bin \
  --connections 4 \
  --buffer-size 65536
```

<redacted>二 `<redacted>`：

```bash
dd if=/dev/urandom of=/tmp/gridflux-src-64m.bin bs=1M count=64 status=none
sha256sum /tmp/gridflux-src-64m.bin

/root/projects/GridFlux/build/gridflux-file-client \
  --host <redacted> \
  --port 19310 \
  --input /tmp/gridflux-src-64m.bin \
  --connections 4 \
  --chunk-size 1048576 \
  --buffer-size 65536
```

<redacted>一传输结束后：

```bash
sha256sum /tmp/gridflux-dst-64m.bin
```

实际结果摘要：

```text
465b47e2bcf6a7fc5fa22e39356af1c37b734577a76c0f36490d5c6b8b562a79  /tmp/gridflux-src-64m.bin
file_client sent_bytes=67108864 elapsed_seconds=0.0328935 throughput_gbps=16.3215
465b47e2bcf6a7fc5fa22e39356af1c37b734577a76c0f36490d5c6b8b562a79  /tmp/gridflux-dst-64m.bin
file_server received_bytes=67108864 elapsed_seconds=0.0345263 throughput_gbps=15.5496
client_status=0 server_status=0
```

## 2026-05-16 Phase 1.2B + Phase 1.3A 文件传输健壮性与文件性能基线自动化

### 实现内容

- 更新 `INDEX.md`、`docs/ROADMAP.md`、`docs/perf/README.md`，同步 Phase 1.2B + Phase 1.3A 状态与文件传输性能工具入口。
- 扩展固定 64 字节 `FrameHeader`：保留 `Data=1`、`Fin=2`，新增 `Complete=3`、`Error=4`，并将原 `reserved0` 字段作为 `statusCode`。
- 新增 `FrameStatusCode`：`Ok`、`InvalidFrame`、`WriteFailed`、`SizeMismatch`、`DuplicateRange`、`RangeOutOfBounds`、`MissingRange`、`OutputExists`、`InternalError`。
- Client 发送 FIN 后必须等待 server 返回最终状态帧；只有所有连接收到 `Complete + Ok` 才返回成功。
- Server 在所有 FIN 到达后执行进度校验和原子 rename；成功后向仍打开的数据连接发送 `Complete + Ok`，失败时尽量发送 `Error + statusCode`。
- 新增 `TransferProgress`，使用简单 offset range 结构跟踪完成进度，检测重复/重叠 range、越界 range、缺失 range 和 total size 不一致。
- Server 输出改为写 `<output>.part.<pid>` 临时文件，成功后 rename 到目标路径；默认拒绝覆盖已有目标，新增 `--overwrite` 与 `--keep-partial`。
- 新增 `tools/test/run_file_transfer_smoke.sh` 并注册为 CTest，覆盖 0 字节、小文件、16MiB 多连接、tail chunk、memory sink 回归和默认拒绝覆盖。
- 新增 `tools/perf/run_file_loopback_matrix.py`，支持文件传输 loopback smoke/full matrix，输出 CSV 与日志。
- 新增 `tools/perf/run_file_private_once.sh`，支持<redacted>一 server + <redacted>二 client 的私网文件传输单次测试；源文件在<redacted>二生成，目标文件在<redacted>一生成。

### 未实现内容

- 未实现 GridFTP 控制面。
- 未实现 manifest、断点续传。
- 未实现 checksum pipeline 或内置 checksum 算法。
- 未实现 per-chunk ACK、ACK 重传窗口。
- 未实现 QUIC、FEC、RDMA、io_uring。
- 未做系统级调参。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：48/48 passed。
- 通过：`ctest --test-dir build -R "FrameTest|ChunkPlannerTest|TransferProgressTest|FileTransferOptionsTest|PosixFileTest" --output-on-failure`
- 通过：`bash -n tools/test/run_file_transfer_smoke.sh tools/perf/run_file_private_once.sh`
- 通过：`python3 -m py_compile tools/perf/run_file_loopback_matrix.py`

### 本机文件传输基线采样

- 通过：`tools/perf/run_file_loopback_matrix.py --build-dir build --smoke --bytes 67108864 --connections 1,4,8 --chunk-sizes 1048576,4194304 --buffer-sizes 65536 --output-dir tools/perf/results`
- CSV：`tools/perf/results/20260516T014818Z_file-loopback-smoke.csv`
- 覆盖 6 组参数：
  - `connections=1, chunk_size=1048576, buffer_size=65536, bytes=67108864`
  - `connections=1, chunk_size=4194304, buffer_size=65536, bytes=67108864`
  - `connections=4, chunk_size=1048576, buffer_size=65536, bytes=67108864`
  - `connections=4, chunk_size=4194304, buffer_size=65536, bytes=67108864`
  - `connections=8, chunk_size=1048576, buffer_size=65536, bytes=67108864`
  - `connections=8, chunk_size=4194304, buffer_size=65536, bytes=67108864`
- 6 组结果均为 `pass`，源/目标 sha256 均一致。

结果摘要：

```text
connections=1 chunk_size=1048576 throughput_gbps=15.5157 result=pass
connections=1 chunk_size=4194304 throughput_gbps=16.4534 result=pass
connections=4 chunk_size=1048576 throughput_gbps=16.6907 result=pass
connections=4 chunk_size=4194304 throughput_gbps=17.0502 result=pass
connections=8 chunk_size=1048576 throughput_gbps=16.9785 result=pass
connections=8 chunk_size=4194304 throughput_gbps=16.9766 result=pass
```

### <redacted>二同步与验证

- 通过：使用 `tools/perf/sync_remote.sh` 同步到 `root@<redacted>:/root/projects/GridFlux/`。
- 同步时 `rsync` 仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：48/48 passed。

### 私网文件传输基线采样

- 通过：`tools/perf/run_file_private_once.sh --remote root@<redacted> --server-host <redacted> --local-build-dir /root/projects/GridFlux/build --remote-build-dir /root/projects/GridFlux/build --connections 4 --bytes 268435456 --chunk-size 1048576 --buffer-size 65536 --output-dir tools/perf/results`
- CSV：`tools/perf/results/20260516T014928Z_file_private_c4_chunk1048576_buf65536_bytes268435456_p19600.csv`

结果摘要：

```text
bytes=268435456
connections=4
chunk_size=1048576
buffer_size=65536
elapsed=0.143735
throughput_gbps=14.9405
source_sha256=0c5ebcf5cb0e4611da9ecf2adf97f248001e00ed6c0bd75a3a44a33ff26d3612
dest_sha256=0c5ebcf5cb0e4611da9ecf2adf97f248001e00ed6c0bd75a3a44a33ff26d3612
result=pass
```

## 2026-05-16 Phase 2A Manifest + Range-based 断点续传核心

### 实现内容

- 新增 `RangeList`，支持严格插入、manifest 加载合并、missing range 计算和 completed bytes 统计。
- 新增 `TransferManifest` 与 `ManifestStore`，使用无第三方依赖的稳定 `key=value` 文本格式；路径使用 hex 编码，completed ranges 使用半开区间 `begin-end`。
- Manifest 路径固定为 `<output>.gridflux.manifest`；可恢复 temp 文件路径改为 `<output>.part.<transfer_id>`。
- 新增 `TransferSession` 薄层，隔离 manifest 状态转换、range 进度和恢复计算，不直接处理 socket 或文件 IO。
- 扩展协议：保留 `Data=1`、`Fin=2`、`Complete=3`、`Error=4`，新增 `SessionInit=5` 与 `ResumeResponse=6`。
- `transfer_id` 通过 `SessionInit` payload 协商，不塞进 64 字节 Data/Fin header；`ResumeResponse` 返回缺失 range list。
- `gridflux-file-client` 新增 `--transfer-id`、`--resume`、`--max-chunks`；未指定 transfer id 且非 resume 时自动生成 32 位十六进制 id。
- `gridflux-file-server` 新增 `--resume`；新传输创建 manifest + stable temp，失败后标记 `Failed` 并保留 temp + manifest，resume 只补传缺失范围。
- 新增 `tools/test/run_file_resume_smoke.sh` 并注册 CTest，覆盖 partial transfer、manifest/temp 保留、resume 补传、sha256/cmp 一致、损坏 manifest 和 total size mismatch 失败路径。

### 未实现内容

- 未实现 GridFTP 控制面、USER/PASS/PASV/STOR/RETR/REST 命令解析。
- 未实现 checksum pipeline 或内置 chunk checksum。
- 未实现 per-chunk ACK、ACK 重传窗口。
- 未实现完整 Mode E、SPAS/SPOR、第三方 server-to-server。
- 未实现 QUIC、FEC、RDMA、io_uring。
- 未实现多文件目录同步。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：69/69 passed。
- 已对本轮新增/修改的 C++ 头文件、源文件和单元测试执行 `clang-format -i`。
- 通过：`ctest --test-dir build -R "TransferManifestTest|ManifestStoreTest|RangeListTest|TransferSessionTest|SessionControlTest" --output-on-failure`
- 通过：`ctest --test-dir build -R "gridflux_file_transfer_smoke|gridflux_file_resume_smoke" --output-on-failure`

### 本机 resume smoke 覆盖

- 64MiB / 4 connections 首次传输使用固定 `transfer_id=phase2a-smoke` 与 `--max-chunks 8` 制造中断。
- 验证最终 output 未提交。
- 验证 `<output>.gridflux.manifest` 和 `<output>.part.<transfer_id>` 存在。
- 使用 server/client 双端 `--resume --transfer-id phase2a-smoke` 补传缺失 range。
- 传输完成后 `cmp` 一致。
- 损坏 manifest 与 total size mismatch 均按预期失败。

### <redacted>二同步与验证

- 本轮未执行<redacted>二同步、构建和私网 resume 验证。
- 当前 shell 未设置 `GRIDFLUX_SSH_PASSWORD`，且 `ssh -o BatchMode=yes root@<redacted>` 返回 `Permission denied (publickey,password)`。
- 待具备远端凭据后可执行：

```bash
export GRIDFLUX_SSH_PASSWORD='***'
tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
sshpass -e ssh root@<redacted> 'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
```

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有非 resume 文件传输命令保持可用；新握手对 CLI 调用透明。
- 现有 file perf scripts 未改变调用方式，metric 正则仍可匹配新增 `transfer_id=` 后缀。

## 2026-05-16 Phase 2B Checksum Pipeline + Corruption/Fault Injection

### 实现内容

- 新增 checksum 模块，内部实现 table-driven CRC32C Castagnoli；默认算法为 `crc32c`，`none` 仅用于性能对照和兼容回归。
- Manifest 升级为 v2，新增 `checksum_algorithm`、`verified_chunks` 和 `manifest_body_crc32c`；serializer 只写 v2，parser 保留 v1 读取能力。
- `verified_chunks` 成为恢复事实源，`completed_ranges` 仅由 verified chunks 派生用于可读性和恢复响应。
- `TransferSession` 新增 `recordVerifiedChunk()`、`missingChunks()` 和 `verifyTempChunks()`；resume 前会重新读取 temp 文件中已 verified chunk 并校验 checksum，损坏 chunk 会被移除并重新补传。
- 协议保持既有编号不变，新增 `ChunkComplete=7`，并在 `SessionInit` payload 中协商 `checksum_algorithm`。
- `gridflux-file-server` 接收 DATA 时同步计算 chunk CRC32C，收到 `ChunkComplete` 后对比 client checksum；checksum mismatch、manifest corrupt、unsupported checksum 均返回明确 `Error` 状态。
- `gridflux-file-client` 每个 chunk 完成后发送 `ChunkComplete`；新增 `--checksum <crc32c|none>`、`--corrupt-chunk <chunk_id>`、`--duplicate-corrupt-chunk <chunk_id>`。
- 新增 `tools/test/run_file_checksum_smoke.sh` 并注册 CTest，覆盖正常 CRC32C、`--checksum none`、partial+resume、temp 损坏修复、manifest checksum 损坏失败、client corrupt chunk 失败、duplicate corrupt chunk 失败。
- 文件传输 perf 脚本新增 `--checksum` 参数和 CSV 字段 `checksum_enabled` / `checksum_algorithm`。
- 新增 `tools/test/run_file_checksum_private_once.sh`，用于<redacted>一 server + <redacted>二 client 的 checksum resume 私网 smoke。

### 未实现内容

- 未实现 GridFTP 控制面、USER/PASS/PASV/STOR/RETR/REST 命令解析。
- 未实现 per-chunk ACK 重传窗口。
- 未实现异步 checksum pipeline、硬件 CRC32C 或 XXH3。
- 未实现完整 Mode E、SPAS/SPOR、第三方 server-to-server。
- 未实现 QUIC、FEC、RDMA、io_uring。
- 未实现多文件目录同步。

### 本机验证

- 通过：`bash -n tools/test/run_file_checksum_smoke.sh tools/test/run_file_checksum_private_once.sh tools/perf/run_file_private_once.sh`
- 通过：`python3 -m py_compile tools/perf/run_file_loopback_matrix.py`
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：81/81 passed。
- 通过：`ctest --test-dir build -R gridflux_file_checksum_smoke --output-on-failure`
- 已对本轮新增/修改的 C++ 头文件、源文件和单元测试执行 `clang-format -i`。

### 本机 checksum 文件传输采样

- 通过 CRC32C 采样：

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksum crc32c \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T042334Z_file-loopback-smoke.csv`
- 结果摘要：

```text
connections=1 checksum=crc32c throughput_gbps=1.09474 result=pass
connections=4 checksum=crc32c throughput_gbps=1.03034 result=pass
source_sha256 == dest_sha256
```

- 通过 `none` 对照采样：

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksum none \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T042336Z_file-loopback-smoke.csv`
- 结果摘要：

```text
connections=1 checksum=none throughput_gbps=13.9955 result=pass
connections=4 checksum=none throughput_gbps=12.0663 result=pass
source_sha256 == dest_sha256
```

### <redacted>二同步与验证

- 第一次同步尝试失败：从 `AGENTS.md` 表格提取了错误列，导致远端认证失败；未修改远端源码。
- 通过：修正环境变量来源后，使用 `tools/perf/sync_remote.sh` 同步到 `root@<redacted>:/root/projects/GridFlux/`。
- 同步时 `rsync` 仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：81/81 passed。

### 私网 checksum resume smoke

- 通过：`tools/test/run_file_checksum_private_once.sh --remote root@<redacted> --server-host <redacted> --local-build-dir /root/projects/GridFlux/build --remote-build-dir /root/projects/GridFlux/build --connections 4 --bytes 67108864 --chunk-size 1048576 --buffer-size 65536 --checksum crc32c --output-dir tools/perf/results`
- 源文件生成在<redacted>二，目标文件生成在<redacted>一。
- 先用 `--max-chunks 8` 制造中断，再双端 `--resume` 补传。
- 日志：
  - `tools/perf/results/20260516T042504Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_p19900_server_partial.log`
  - `tools/perf/results/20260516T042504Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_p19900_client_partial.log`
  - `tools/perf/results/20260516T042504Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_p19900_server_resume.log`
  - `tools/perf/results/20260516T042504Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_p19900_client_resume.log`
- 结果摘要：

```text
bytes=67108864
connections=4
chunk_size=1048576
buffer_size=65536
checksum=crc32c
source_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
dest_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
result=pass
```

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有非 resume 文件传输命令保持可用；不传 `--checksum` 时默认启用 `crc32c`。
- `--checksum none` 可用于性能对照，但可靠 resume 推荐路径是 CRC32C。
- 当前 CRC32C 为纯软件 table-driven 实现，本机 loopback 吞吐明显低于 `none`；后续可评估硬件 CRC32C、批量 manifest flush 或异步 checksum worker。
- 最终 sha256 仍只用于脚本验收；内部恢复事实源是 manifest v2 `verified_chunks`。

## 2026-05-16 Phase 2C Checksum 性能优化 + 恢复策略收敛

### 实现内容

- 新增 CRC32C backend 选择：`auto`、`software`、`hardware`。
- 新增 x86 SSE4.2 CRC32C 硬件实现文件 `src/checksum/crc32c_hw_x86.cpp`，单独用 `-msse4.2` 编译；非 x86 或不可用编译器走 stub，不给整个 `gridflux_core` 增加 SSE4.2 依赖。
- `ChecksumComputer` 默认 `auto`，运行时通过 `__builtin_cpu_supports("sse4.2")` 选择 hardware，否则回退 software；显式 `hardware` 不可用时返回明确错误。
- 新增 `gridflux-checksum-bench` 和 `tools/benchmark/run_checksum_bench.py`，用于记录 CRC32C backend microbenchmark CSV。
- 新增 `TransferSessionConfig`，集中承载 `transfer_id`、total/chunk size、connections、resume、checksum algorithm/backend 和 manifest flush 策略，为后续 GridFTP 控制面映射准备内部入口。
- `gridflux-file-server/client` 新增 `--checksum-backend auto|software|hardware`；server 新增 `--manifest-flush-interval-chunks <N>`，默认 `16`。
- `TransferSession` 改为批量 flush manifest：每 `16` 个 verified chunk 默认保存一次；失败、resume preflight、commit 前和 `Failed/Committed` 状态转换强制 flush。
- 新增恢复统计输出：`checksum_backend`、`skipped_bytes`、`resent_bytes`、`verified_bytes`、`loaded_verified_chunks`、`removed_corrupt_chunks`、`missing_chunks`、`manifest_flush_policy`、`manifest_flush_count`。
- 文件 perf 脚本新增 `--checksum-backend` 和对应 CSV 字段；私网 checksum resume smoke 支持 backend 参数。

### 未实现内容

- 未实现 GridFTP 控制面、USER/PASS/PASV/STOR/RETR/REST 命令解析。
- 未实现完整 Mode E、SPAS/SPOR、第三方 server-to-server。
- 未实现异步 checksum worker、通用线程池、ACK 重传窗口。
- 未实现 QUIC、FEC、RDMA、io_uring。
- 未实现多文件目录同步或系统级调参。

### 本机环境确认

- 执行：`lscpu` 与 `lscpu | rg -n "Flags|sse4_2"`。
- 结果：本机为 `x86_64` / Intel(R) Xeon(R) 6982P-C，CPU flags 包含 `sse4_2`。

### 本机验证

- 通过：`clang-format -i` 已作用于本轮新增/修改的 C++ header/source/test 文件。
- 通过：`python3 -m py_compile tools/benchmark/run_checksum_bench.py tools/perf/run_file_loopback_matrix.py`
- 通过：`bash -n tools/perf/run_file_private_once.sh tools/test/run_file_checksum_private_once.sh`
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：84/84 passed。

### Checksum benchmark

- 手动 smoke：

```text
./build/gridflux-checksum-bench --backend software --bytes 67108864 --iterations 5
throughput_gbps=2.40673 checksum=908575515

./build/gridflux-checksum-bench --backend auto --bytes 67108864 --iterations 5
backend=hardware throughput_gbps=24.0553 checksum=908575515

./build/gridflux-checksum-bench --backend hardware --bytes 67108864 --iterations 5
backend=hardware throughput_gbps=17.3772 checksum=908575515
```

- 通过：`tools/benchmark/run_checksum_bench.py --build-dir build --bytes 67108864,268435456 --output-dir tools/perf/results`
- CSV：`tools/perf/results/20260516T055052Z_checksum_bench.csv`
- CSV 摘要：

```text
64MiB software throughput_gbps=2.51781 result=pass
64MiB auto     backend=hardware throughput_gbps=45.1199 result=pass
64MiB hardware backend=hardware throughput_gbps=39.2493 result=pass
256MiB software throughput_gbps=2.51956 result=pass
256MiB auto     backend=hardware throughput_gbps=47.8127 result=pass
256MiB hardware backend=hardware throughput_gbps=47.5392 result=pass
```

- 结论：`auto` 在本机选择 hardware；64MiB batch benchmark 中 auto/software 约 `17.9x`，达到 Phase 2C 至少 `4x` 目标。

### 本机文件传输采样

- 通过 CRC32C auto：

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T055121Z_file-loopback-smoke.csv`
- 结果摘要：

```text
connections=1 checksum=crc32c checksum_backend=hardware throughput_gbps=7.28194 result=pass
connections=4 checksum=crc32c checksum_backend=hardware throughput_gbps=7.23309 result=pass
manifest_flush_policy=every_16_chunks manifest_flush_count=7
source_sha256 == dest_sha256
```

- 通过 `none` 对照：

```bash
tools/perf/run_file_loopback_matrix.py \
  --build-dir build \
  --smoke \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksum none \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T055128Z_file-loopback-smoke.csv`
- 结果摘要：

```text
connections=1 checksum=none throughput_gbps=15.8585 result=pass
connections=4 checksum=none throughput_gbps=16.1389 result=pass
source_sha256 == dest_sha256
```

- Phase 2B software CRC32C baseline：1 connection `1.09474 Gbps`，4 connections `1.03034 Gbps`。
- Phase 2C CRC32C auto 相对 Phase 2B：1 connection 约 `6.65x`，4 connections 约 `7.02x`，达到至少 `2x` 目标。
- 同轮 CRC32C auto 与 `none` 仍有约 `54%-55%` 差距，未达到小于 `30%` 的报告目标；记录为后续优化项（可能方向：减少 final preflight 重读、批量 manifest/verify 策略、异步 checksum worker 或更大 chunk 采样）。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 第一次远程 build 命令因 shell 同行展开导致 `SSHPASS` 未正确设置，返回 `Permission denied, please try again.`；随后分步设置 `GRIDFLUX_SSH_PASSWORD` / `SSHPASS` 后重试通过。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：84/84 passed。

### 私网 checksum resume smoke

- 通过：

```bash
tools/test/run_file_checksum_private_once.sh \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --output-dir tools/perf/results
```

- 日志：
  - `tools/perf/results/20260516T055306Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_auto_p19900_server_partial.log`
  - `tools/perf/results/20260516T055306Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_auto_p19900_client_partial.log`
  - `tools/perf/results/20260516T055306Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_auto_p19900_server_resume.log`
  - `tools/perf/results/20260516T055306Z_file_checksum_private_resume_c4_chunk1048576_buf65536_bytes67108864_crc32c_auto_p19900_client_resume.log`
- 结果摘要：

```text
source_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
dest_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
server_resume checksum_backend=hardware throughput_gbps=7.8314 skipped_bytes=3145728 resent_bytes=63963136 verified_bytes=67108864 manifest_flush_policy=every_16_chunks manifest_flush_count=6
client_resume checksum_backend=hardware throughput_gbps=7.59491 skipped_bytes=3145728 resent_bytes=63963136 verified_bytes=67108864
result=pass
```

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有非 resume 文件传输命令保持可用；不传新参数时等价于 `--checksum crc32c --checksum-backend auto --manifest-flush-interval-chunks 16`。
- `--checksum none` 仍可用于性能对照和兼容回归。
- manifest v2 `verified_chunks` 仍是恢复事实源；最终 sha256 仍只用于脚本验收。
- 批量 manifest flush 在崩溃后可能重传少量已写 temp chunk，但不会误提交 output。

## 2026-05-16 Phase 3A GridFTP 控制面最小 STOR 映射

### 实现内容

- 新增 `gridflux-gridftp-server` 控制面可执行文件。
- 新增 `gridflux::protocol::control` parser/options/session/server 模块，支持 `USER`、`PASS`、`TYPE I`、`SYST`、`FEAT`、`PWD`、`NOOP`、`QUIT`、`EPSV`、`PASV`、`OPTS PARALLELISM=<N>`、`OPTS RETR Parallelism=<N>`、`REST GFID:<transfer_id>` 和 `STOR <path>`。
- 控制面回复码覆盖本阶段需要的 `220`、`331`、`230`、`200`、`211`、`215`、`257`、`229`、`227`、`150`、`226`、`350`、`421`、`502`、`530`、`550`。
- `STOR` 数据连接继续使用 GridFlux framed protocol，不兼容普通 FTP raw stream STOR。
- `STOR` path 被限制在 `--root` 内；拒绝绝对路径、`..`、空路径、目录路径和缺失父目录。
- 新上传由控制面生成 `GFID:<transfer_id>`；resume 使用 `REST GFID:<transfer_id>` 映射到 manifest v2 `verified_chunks` / missing ranges。
- `gridflux-file-server` 接收逻辑新增 `runFileTransferServerOnListener()`，供控制面复用已准备好的 passive listener；旧 `gridflux-file-server` CLI 仍走原入口。
- `FileTransferOptions.transferId` 在 server 侧非空时强制校验 data `SessionInit.transfer_id`，旧 CLI 默认空值不受影响。
- 新增 `tools/test/run_gridftp_control_stor_smoke.py`、`run_gridftp_control_resume_smoke.py` 并注册 CTest。
- 新增 `tools/test/run_gridftp_control_private_once.py`，用于<redacted>一 control/data server + <redacted>二 framed data client 的私网 STOR/resume smoke。

### 未实现内容

- 未实现普通 FTP raw stream STOR。
- 未实现 RETR/download、LIST、SIZE、PORT/EPRT。
- 未实现 TLS、GSI、DCAU、PROT、SPAS/SPOR、完整 Mode E、第三方 server-to-server。
- 未实现多文件目录同步、io_uring 或系统级调参。

### 本机验证

- 曾误对 `CMakeLists.txt` 执行 `clang-format`，导致 CMake 语法被破坏；已手工恢复 CMakeLists，并重新 configure/build/ctest 验证通过。后续仍应只对 C++ 源码和头文件执行 clang-format。
- 通过：`python3 -m py_compile tools/test/run_gridftp_control_stor_smoke.py tools/test/run_gridftp_control_resume_smoke.py tools/test/run_gridftp_control_private_once.py`
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 测试结果：99/99 passed。
- 通过：`ctest --test-dir build -R "Gridftp|Control|gridflux_gridftp|file_transfer|resume|checksum" --output-on-failure`
- 过滤测试结果：22/22 passed。

### 本机 GridFTP control smoke

- CTest `gridflux_gridftp_control_stor_smoke` 通过：
  - 启动 `gridflux-gridftp-server --host 127.0.0.1 --root <tmp>`。
  - Python control client 执行 `USER/PASS/TYPE I/EPSV/STOR uploaded.bin`。
  - 使用 `gridflux-file-client --transfer-id <id>` 连接 EPSV data port。
  - 收到 `226` 后校验 sha256 一致。
- CTest `gridflux_gridftp_control_resume_smoke` 通过：
  - 首次 STOR 使用 `--max-chunks` 制造中断，control 返回 `550`。
  - 确认 output 未提交，manifest 和 `.part.<transfer_id>` 存在。
  - 新会话执行 `REST GFID:<transfer_id>` + `STOR`，data client 使用 `--resume` 补传。
  - 收到 `226` 后校验 sha256 一致，并确认 server log 包含 `skipped_bytes`、`verified_bytes`、`checksum_backend`。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：99/99 passed。

### 私网 GridFTP control STOR/resume smoke

- 通过：

```bash
tools/test/run_gridftp_control_private_once.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --root /tmp/gridflux-gridftp-private-root \
  --port 2141 \
  --data-port-base 20400 \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --output-dir tools/perf/results
```

- 源文件生成在<redacted>二，control/data server 运行在<redacted>一。
- 完整 STOR 结果：

```text
transfer_id=9aeeaf2a457ecbcd03666fedfa42d8ba
received_bytes=67108864
checksum_backend=hardware
throughput_gbps=7.84728
source_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
dest_sha256=5c8a41a9b8d7fc418ba77b0312efc461de86740ef476f4b53adab9313c4d1562
result=pass
```

- REST resume 结果：

```text
transfer_id=c98bce75705b0e5261afee0b076857d1
received_bytes=67108864
checksum_backend=hardware
skipped_bytes=2097152
resent_bytes=65011712
verified_bytes=67108864
throughput_gbps=7.96243
result=pass
```

- 私网日志：`tools/perf/results/20260516T064417Z_gridftp_control_private.log`

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有 `gridflux-file-server` / `gridflux-file-client` CLI 行为保持不变。
- Phase 3A 的外部 client 必须是 GridFlux-aware client：控制面使用 FTP/GridFTP 风格命令，数据面使用现有 GridFlux framed protocol。
- `REST GFID:<transfer_id>` 是控制面映射入口；内部恢复事实源仍是 manifest v2 `verified_chunks`，不退回单 offset。

## 2026-05-16 Phase 3B GridFTP 控制面 RETR/download framed 映射

### 实现内容

- `gridflux-gridftp-server` 新增 `RETR <path>` 控制命令。
- `RETR` 前置条件与 STOR 一致：必须登录、`TYPE I`、已执行 `EPSV` 或 `PASV`。
- `RETR` path 被限制在 `--root` 内；拒绝绝对路径、`..`、空路径、目录路径和不存在文件。
- `RETR` 数据连接继续使用 GridFlux framed protocol，不兼容普通 FTP raw stream RETR。
- 新增 `gridflux-file-download-client`，作为 GridFlux-aware framed RETR 接收端。
- 新增 framed download sender/receiver：
  - server-side sender 发送 `SessionInit`、DATA、`ChunkComplete`、FIN。
  - download client 回复完整文件 `ResumeResponse`，按 offset 写入 `<output>.part.<transfer_id>`，校验 chunk checksum 后返回 `Complete + Ok`，成功后 rename 到目标路径。
- Phase 3B 明确不实现 RETR resume；`REST GFID:<transfer_id>` 后执行 `RETR` 返回 `550`，下载恢复留到 Phase 3C。
- 新增 `FileDownloadOptions` 和 `gridflux-file-download-client` CLI：
  - `--host`、`--port`、`--output`、`--connections`、`--buffer-size`、`--transfer-id`、`--checksum`、`--checksum-backend`、`--overwrite`。
- 新增 `tools/test/run_gridftp_control_retr_smoke.py`，覆盖 crc32c/none 两组 loopback framed RETR。
- 新增 `tools/test/run_gridftp_control_retr_rest_unsupported.py`，覆盖 `REST GFID + RETR` 返回 `550`。
- 新增 `tools/test/run_gridftp_control_retr_private_once.py`，用于<redacted>一 control/data server + <redacted>二 framed download client 的私网 RETR smoke。

### 未实现内容

- 未实现普通 FTP raw stream RETR。
- 未实现 RETR resume、download manifest、download verified_chunks 或 `REST offset`。
- 未实现 LIST、SIZE、PORT/EPRT、TLS、GSI、DCAU、PROT、SPAS/SPOR、完整 Mode E、第三方 server-to-server。
- 未实现多文件目录同步、io_uring 或系统级调参。

### 本机验证

- 通过：`python3 -m py_compile tools/test/run_gridftp_control_retr_smoke.py tools/test/run_gridftp_control_retr_rest_unsupported.py tools/test/run_gridftp_control_retr_private_once.py`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build -R "ControlCommandTest|ControlSessionTest|ControlOptionsTest|gridflux_gridftp_control_retr" --output-on-failure`
- 过滤测试结果：17/17 passed。
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：106/106 passed。

### 本机 GridFTP control RETR smoke

- CTest `gridflux_gridftp_control_retr_smoke` 通过：
  - 启动 `gridflux-gridftp-server --host 127.0.0.1 --root <tmp>`。
  - root 内创建 `source.bin`。
  - Python control client 执行 `USER/PASS/TYPE I/EPSV/RETR source.bin`。
  - 使用 `gridflux-file-download-client --transfer-id <id>` 连接 EPSV data port。
  - crc32c auto 和 checksum none 两组均收到 `226`，sha256 一致。
- CTest `gridflux_gridftp_control_retr_rest_unsupported` 通过：
  - 执行 `REST GFID:<id>`、`EPSV`、`RETR source.bin`。
  - control server 返回 `550 RETR resume is not supported in Phase 3B`。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响本次构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：106/106 passed。

### 私网 GridFTP control RETR smoke

- 通过：

```bash
python3 tools/test/run_gridftp_control_retr_private_once.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --control-port 2121 \
  --root /tmp/gridflux-gridftp-retr-private-root \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --download-output /tmp/gridflux-gridftp-private-retr.bin \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --data-port-base 20300 \
  --output-dir tools/perf/results
```

- 源文件生成在<redacted>一 server root，download client 在<redacted>二写入目标文件。
- 私网结果：

```text
transfer_id=b79b4229944db8898bdf766e9cc83014
sent_bytes=67108864
checksum_backend=hardware
throughput_gbps=12.9628
source_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
dest_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
result=pass
```

- 私网日志：`tools/perf/results/20260516T094939Z_gridftp_control_retr_private.log`
- 本机和<redacted>二检查均未发现遗留 `gridflux-gridftp-server`、`gridflux-file-client`、`gridflux-file-download-client` 或 `gridflux-file-server` 进程。

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有 `gridflux-file-server` / `gridflux-file-client` CLI 行为保持不变。
- Phase 3A STOR loopback smoke 和 STOR REST resume smoke 保持在 full CTest 中。
- Phase 3B 的外部 RETR client 必须是 GridFlux-aware client：控制面使用 FTP/GridFTP 风格命令，数据面使用现有 GridFlux framed protocol。

## 2026-05-16 Phase 3C RETR/download resume

### 实施内容

- 新增下载端 manifest：
  - `include/gridflux/checkpoint/download_manifest.h`
  - `src/checkpoint/download_manifest.cpp`
  - 路径规则：`<output>.gridflux.download.manifest`，临时文件 `<output>.part.<transfer_id>`。
  - 记录 `transfer_id`、`source_path`、`target_path`、`temp_path`、`total_size`、`chunk_size`、`checksum_algorithm`、`verified_chunks` 和 `manifest_body_crc32c`。
- 新增下载端 session：
  - `include/gridflux/core/session/download_session.h`
  - `src/core/session/download_session.cpp`
  - 支持 create/resume、verified chunk 记录、missing ranges 派生、temp verified chunk preflight、corrupt chunk 移出 verified set。
- 扩展 `SessionInitPayload`：
  - 旧 payload 保持兼容。
  - RETR sender 可追加可选 `source_path`，download client resume 用于校验本地 manifest 是否对应同一源路径。
- `gridflux-file-download-client` 新增：
  - `--resume`
  - `--max-chunks <N>`，用于故障注入。
  - 成功输出 `skipped_bytes`、`resent_bytes`、`verified_bytes`、`removed_corrupt_chunks` 和 `manifest_flush_count`。
- `gridflux-gridftp-server` 的 `REST GFID:<transfer_id> + RETR <path>` 从 Phase 3B 的 `550 unsupported` 改为支持真正 resume：
  - 无 REST 时生成新 transfer id。
  - 有 REST 时复用 token，sender 进入 resume mode。
  - `REST offset` 仍由 parser 拒绝。
- RETR sender 根据 download client 返回的 `ResumeResponse.missing_ranges` 只发送缺失 chunk，不做完整重传伪 resume。
- 新增/更新测试：
  - `DownloadManifestTest`
  - `DownloadSessionTest`
  - `SessionControlTest.EncodesAndDecodesSessionInitSourcePathExtension`
  - `tools/test/run_gridftp_control_retr_resume_smoke.py`
  - `tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py`
  - `tools/test/run_gridftp_control_retr_private_once.py --resume`
- 删除 Phase 3B 的 `tools/test/run_gridftp_control_retr_rest_unsupported.py`，CTest 改为 RETR resume / corrupt resume。

### 未实现内容

- 未实现普通 FTP raw stream STOR/RETR。
- 未实现 RETR `REST offset`。
- 未实现 LIST、SIZE、PORT/EPRT、TLS、GSI、DCAU、PROT、SPAS/SPOR、完整 Mode E、第三方 server-to-server。
- 未实现多文件目录同步、io_uring 或系统级调参。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：115/115 passed。
- 通过：`ctest --test-dir build -R "Gridftp|Control|gridflux_gridftp|download|resume|checksum|Manifest" --output-on-failure`
- 过滤测试结果：43/43 passed。
- 通过：`python3 -m py_compile tools/test/run_gridftp_control_retr_smoke.py tools/test/run_gridftp_control_retr_resume_smoke.py tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py tools/test/run_gridftp_control_retr_private_once.py`

### 本机 GridFTP control RETR smoke

- 通过：`python3 tools/test/run_gridftp_control_retr_smoke.py --build-dir build`
  - crc32c auto 与 checksum none 两组均 pass，sha256 一致。
- 通过：`python3 tools/test/run_gridftp_control_retr_resume_smoke.py --build-dir build`
  - 首次 RETR 使用 `--max-chunks` 中断。
  - output 未提交，download manifest 与 temp 文件保留。
  - 新控制会话执行 `REST GFID:<id>` + `RETR source.bin`。
  - download client `--resume --transfer-id <id>` 补传 missing chunks，sha256 一致。
  - 日志包含 `skipped_bytes`、`resent_bytes`、`verified_bytes`。
- 通过：`python3 tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py --build-dir build`
  - partial 后篡改 temp 中已 verified chunk。
  - resume preflight 检测并移除 corrupt chunk，补传后 sha256 一致。
  - 日志包含 `removed_corrupt_chunks=1`。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响构建和测试。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：115/115 passed。

### 私网 GridFTP control RETR resume smoke

- 通过：

```bash
python3 tools/test/run_gridftp_control_retr_private_once.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --control-port 2121 \
  --root /tmp/gridflux-gridftp-retr-private-root \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --download-output /tmp/gridflux-gridftp-private-retr.bin \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --data-port-base 20300 \
  --output-dir tools/perf/results \
  --resume
```

- 源文件生成在<redacted>一 server root，download client 在<redacted>二先 `--max-chunks` 中断，再 `--resume` 补传。
- 私网结果：

```text
transfer_id=1689c9f4bb3ea576a04def7340840423
sent_bytes=55574528
skipped_bytes=11534336
resent_bytes=55574528
checksum_backend=hardware
source_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
dest_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
result=pass
```

- 私网日志：`tools/perf/results/20260516T121734Z_gridftp_control_retr_private.log`
- 本机和<redacted>二检查均未发现遗留 `gridflux-gridftp-server`、`gridflux-file-client`、`gridflux-file-download-client` 或 `gridflux-file-server` 进程。

### 备注

- 现有 `gridflux-server` / `gridflux-client` memory sink 行为保持不变。
- 现有 `gridflux-file-server` / `gridflux-file-client` CLI 行为保持不变。
- STOR full/resume、file transfer resume、checksum smoke 仍在 full CTest 中。
- Phase 3C 的外部 RETR client 必须是 GridFlux-aware client：控制面使用 FTP/GridFTP 风格命令，数据面使用 GridFlux framed protocol。
- 下载恢复事实源在 download client 本地 manifest，不退回单 offset，也不做完整重传伪 resume。

## 2026-05-16 Phase 3D GridFTP 控制面兼容扩展与测试工具收敛

### 实施内容

- `gridflux-gridftp-server` 新增控制命令：
  - `SIZE <path>`
  - `MDTM <path>`
  - `CWD <path>`
  - `CDUP`
  - `LIST [path]`
  - `NLST [path]`
- `ControlSession` 新增 root-relative 当前工作目录，初始为 `/`；`PWD` 返回当前目录，`CWD/CDUP` 只改变 control session 状态，不改变进程工作目录。
- 新增统一 root-confined path resolver，供 STOR、RETR、SIZE、MDTM、LIST、NLST、CWD 共用：
  - 支持相对当前工作目录解析路径。
  - 拒绝绝对路径、`..` 逃逸和符号链接逃逸 root。
  - STOR 可解析目标文件；RETR/SIZE/MDTM 必须是普通文件；LIST/NLST/CWD 必须是目录。
- `LIST/NLST` 使用 passive data listener 发送 FTP-style ASCII 目录元数据：
  - NLST 每行一个 entry name。
  - LIST 使用稳定格式：`type size UTC-mtime name`。
  - 输出不泄露 server root 外真实路径。
- FEAT 新增 `SIZE`、`MDTM`、`LIST`、`NLST`、`CWD`、`CDUP`。
- 新增测试脚本：
  - `tools/test/run_gridftp_control_metadata_smoke.py`
  - `tools/test/run_gridftp_control_list_smoke.py`
  - `tools/test/run_gridftp_control_metadata_private_once.py`
- 统一 Python helper 执行位，删除本地 `tools/test/__pycache__`、`tools/perf/__pycache__` 和 `tools/benchmark/__pycache__` 工作区残留。

### 未实现内容

- 未实现普通 FTP raw stream STOR/RETR。
- 未实现 PORT/EPRT、MLST/MLSD、TLS、GSI、DCAU、PROT、SPAS/SPOR、完整 Mode E、第三方 server-to-server。
- 未实现多文件目录同步、io_uring 或系统级调参。
- 未在普通 sync 中强删<redacted>二历史 `build-private-verify-20260515T163633Z` 残留目录；该目录继续记录为环境残留，必要时人工确认后清理。

### 本机已执行验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：122/122 passed。
- 通过：`ctest --test-dir build -R "Gridftp|Control|gridflux_gridftp|download|resume|checksum|Manifest|Session|List|Size|Mdtm" --output-on-failure`
- 过滤测试结果：62/62 passed。
- 通过：`ctest --test-dir build -R "ControlCommandTest|ControlSessionTest|ControlOptionsTest" --output-on-failure`
- 通过：`python3 -m py_compile tools/test/run_gridftp_control_metadata_smoke.py tools/test/run_gridftp_control_list_smoke.py tools/test/run_gridftp_control_metadata_private_once.py`
- 通过：`python3 tools/test/run_gridftp_control_metadata_smoke.py --build-dir build`
- 通过：`python3 tools/test/run_gridftp_control_list_smoke.py --build-dir build`
- 通过：`python3 tools/test/run_gridftp_control_resume_smoke.py --build-dir build`
- 通过：`python3 tools/test/run_gridftp_control_retr_resume_smoke.py --build-dir build`
- 通过：`python3 tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py --build-dir build`

### 待完成验收

无。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响构建和测试。本次同步脚本已新增 `__pycache__/` 和 `*.pyc` 排除规则。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：122/122 passed。

### 私网 GridFTP control metadata/list smoke

- 通过：

```bash
python3 tools/test/run_gridftp_control_metadata_private_once.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --control-port 2121 \
  --root /tmp/gridflux-gridftp-metadata-private-root \
  --local-build-dir /root/projects/GridFlux/build \
  --data-port-base 20300 \
  --output-dir tools/perf/results
```

- 覆盖<redacted>二控制客户端连接<redacted>一 `gridflux-gridftp-server`，执行 `SIZE`、`MDTM`、`PWD`、`CWD`、`CDUP`、`EPSV + NLST` 和 `EPSV + LIST`。
- 私网日志：`tools/perf/results/20260516T133210Z_gridftp_control_metadata_private.log`

### 私网 RETR resume 回归

- 通过：

```bash
python3 tools/test/run_gridftp_control_retr_private_once.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --control-port 2121 \
  --root /tmp/gridflux-gridftp-retr-private-root \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --download-output /tmp/gridflux-gridftp-private-retr.bin \
  --connections 4 \
  --bytes 67108864 \
  --chunk-size 1048576 \
  --buffer-size 65536 \
  --checksum crc32c \
  --checksum-backend auto \
  --data-port-base 20300 \
  --output-dir tools/perf/results \
  --resume
```

- 私网结果：

```text
transfer_id=cd710d9a70ce9847ccab4939a6715ab9
source_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
dest_sha256=03d1082dd294736da57b0bb3b293af5e18de5f15398ba044f8053624048dbc0d
result=pass
```

- 私网日志：`tools/perf/results/20260516T133210Z_gridftp_control_retr_private.log`

### 最终检查

- 通过：`clang-format --dry-run --Werror` 覆盖本次修改的 C++ 文件。
- 通过：`bash -n tools/perf/sync_remote.sh`
- 本机和<redacted>二检查均未发现遗留 `gridflux-gridftp-server`、`gridflux-file-client`、`gridflux-file-download-client` 或 `gridflux-file-server` 业务进程；`pgrep` 输出仅包含检查命令自身。
- 本地 `tools/test`、`tools/perf`、`tools/benchmark` 下无 `__pycache__` 残留。

## 2026-05-16 Phase 4A 私网性能基线矩阵、指标收敛与 io_uring 前置评估

### 实施内容

- 新增 `tools/perf/run_gridftp_private_matrix.py`：
  - 从<redacted>一启动 `gridflux-gridftp-server`。
  - 通过 SSH 在<redacted>二运行 `gridflux-file-client` 或 `gridflux-file-download-client`。
  - 支持 `--smoke` / `--full`，必须显式选择，避免误跑长矩阵。
  - 支持 `stor`、`retr`、`stor-resume`、`retr-resume`。
  - 为每个 case 分配唯一 control/data port、server root、remote/local 临时路径和 transfer id。
  - 输出 CSV 到 `tools/perf/results/`，并保留 server/client 原始日志路径。
- CSV 统一记录：
  - direction、bytes、connections、chunk size、buffer size、checksum algorithm/backend。
  - elapsed、throughput、skipped/resent/verified bytes、manifest flush count。
  - source/dest sha256、result、server/client log。
  - <redacted>一/<redacted>二 hostname、kernel、CPU flags、文件系统类型和可用空间。
- 文档新增 `docs/perf/PHASE4A_BASELINE.md`，记录 Phase 4A 基线目标、运行命令、结果路径和瓶颈判断口径。
- `docs/perf/README.md` 新增 GridFTP-like framed 私网 smoke/full/代表性 1GiB 命令和 CSV 字段说明。
- `docs/ROADMAP.md` 记录 Phase 4A 决策：不直接引入 io_uring，先用现有 epoll/pread/pwrite 基线矩阵决定优化顺序。

### 已执行验证

- 通过：`python3 -m py_compile tools/perf/run_gridftp_private_matrix.py`
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：122/122 passed。
- 探针通过：`tools/perf/run_gridftp_private_matrix.py --smoke --directions stor --bytes 1048576 --connections 1 --chunk-sizes 1048576 --buffer-sizes 65536 --checksums none ...`
- 探针通过：`tools/perf/run_gridftp_private_matrix.py --smoke --directions retr --bytes 1048576 --connections 1 --chunk-sizes 1048576 --buffer-sizes 65536 --checksums none ...`

### 私网 GridFTP-like framed smoke matrix

- 首次 smoke 发现脚本 bug：RETR case 生成的 server-side 源文件名与控制面 `RETR` 请求文件名不一致，导致 8 个 RETR case 正确返回 `550 RETR path does not exist`。已修复 `tools/perf/run_gridftp_private_matrix.py`，失败 CSV 保留为 `tools/perf/results/20260516T151000Z_gridftp-private-matrix-smoke.csv`。
- 修复后正式 smoke 通过：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T151103Z_gridftp-private-matrix-smoke.csv`
- 结果：16/16 pass，`source_sha256 == dest_sha256`。
- 摘要：

```text
STOR 64MiB  conn=1 crc32c hardware 8.17878 Gbps
STOR 64MiB  conn=1 none            15.2021 Gbps
STOR 64MiB  conn=4 crc32c hardware 8.05871 Gbps
STOR 64MiB  conn=4 none            16.9174 Gbps
STOR 128MiB conn=1 crc32c hardware 8.17215 Gbps
STOR 128MiB conn=1 none            15.839 Gbps
STOR 128MiB conn=4 crc32c hardware 8.28839 Gbps
STOR 128MiB conn=4 none            14.0477 Gbps
RETR 64MiB  conn=1 crc32c hardware 7.50698 Gbps
RETR 64MiB  conn=1 none            14.129 Gbps
RETR 64MiB  conn=4 crc32c hardware 7.82893 Gbps
RETR 64MiB  conn=4 none            13.2575 Gbps
RETR 128MiB conn=1 crc32c hardware 7.57327 Gbps
RETR 128MiB conn=1 none            12.4479 Gbps
RETR 128MiB conn=4 crc32c hardware 8.30637 Gbps
RETR 128MiB conn=4 none            13.4099 Gbps
```

### 私网代表性 1GiB 样本

- 通过：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T151131Z_gridftp-private-matrix-smoke.csv`
- 结果：2/2 pass，`source_sha256 == dest_sha256`。
- 摘要：

```text
STOR 1GiB conn=8 chunk=4MiB buffer=256KiB crc32c hardware throughput_gbps=0.656442
RETR 1GiB conn=8 chunk=4MiB buffer=256KiB crc32c hardware throughput_gbps=0.973053
```

### 私网 resume 指标探针

- 通过：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor-resume,retr-resume \
  --bytes 16777216 \
  --connections 4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c \
  --max-chunks 2 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T151242Z_gridftp-private-matrix-smoke.csv`
- 结果：2/2 pass，`source_sha256 == dest_sha256`。
- 摘要：

```text
STOR resume throughput_gbps=7.17879 skipped_bytes=1048576 resent_bytes=15728640 verified_bytes=16777216
RETR resume throughput_gbps=4.86455 skipped_bytes=5242880 resent_bytes=11534336 verified_bytes=16777216
```

### Phase 4A 初步瓶颈判断

- 64MiB/128MiB smoke 中 `checksum none` 明显快于 CRC32C hardware，checksum 仍是主要开销之一。
- 1/4 connections 在 smoke 中未呈现线性提升，说明瓶颈不只是单连接 TCP 窗口。
- 1GiB 样本吞吐显著低于 64MiB/128MiB smoke，优先怀疑磁盘路径、临时文件落盘、cache/writeback、manifest flush 或测试文件生成/读取路径；需要 fio/iperf 对照和更多 chunk/buffer 点后再决定是否切 io_uring。
- Phase 4A 暂不改变 IO 后端，先补齐 full/代表性矩阵和对照数据。

### 待执行验证

无。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二存在旧的 `build-private-verify-20260515T163633Z` 非空目录未删除；源码同步完成，未影响构建和测试，未强删该历史残留目录。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- 测试结果：122/122 passed。

### 最终检查

- 已清理本机 `tools/perf/__pycache__` 与 `tools/benchmark/__pycache__`。
- 最终文档/脚本同步已再次执行；首次重试因本地凭据提取字段不匹配而走普通 SSH 失败，随后改用 `AGENTS.md` 表格列解析并通过 `GRIDFLUX_SSH_PASSWORD` 完成同步，未打印密码。
- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录按约束未强删。
- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。

## 2026-05-17 Phase 4D 存储落盘路径优化与 IO 后端抽象前置

### 实施内容

- 修正 `docs/ROADMAP.md` 中 Phase 4C “需补充 median 结论”的旧表述，改为引用 `docs/perf/PHASE4C_STORAGE.md` 的 observed storage bench 与 repeat matrix median。
- 新增轻量 file IO 层：
  - `FileIoBackendKind { Posix }`
  - `FileIoAdvice { Off, Sequential, Noreuse, DontNeed, SequentialDontNeed }`
  - `FileIoConfig`
  - `FileIoStats`
  - POSIX concrete helper，内部继续使用 `PosixFile` / `pread` / `pwrite` / `posix_fadvise`。
- STOR server temp write、upload client source read、RETR sender source read、download client temp write 已统一经过 file IO helper。
- 新增 CLI 参数：
  - `--file-io-backend posix`
  - `--file-io-buffer-size <N>`
  - `--file-io-advice off|sequential|noreuse|dontneed|sequential_dontneed`
- STOR/RETR 日志追加：
  - `file_io_backend`
  - `file_io_buffer_size`
  - `file_io_advice`
  - `stage_read_calls`
  - `stage_write_calls`
  - `stage_read_avg_bytes_per_call`
  - `stage_write_avg_bytes_per_call`
  - `file_io_wait_seconds`
  - `file_io_wait_bytes`
- `gridflux-storage-bench` 增加 per-iteration raw output、aggregate output、file IO advice 和 call count 指标。
- `tools/benchmark/run_storage_bench.py` 增加 raw CSV + summary CSV。
- `tools/perf/run_gridftp_private_matrix.py` 增加 `--file-io-buffer-sizes`、`--file-io-advices`、`--file-io-backend`，并将 file IO 参数纳入 summary 分组。

### 保持不变

- 未实现 io_uring。
- 未实现 raw FTP STOR/RETR。
- 未实现 TLS/GSI、MLST/MLSD、多文件目录同步。
- 默认仍为 `file_io_buffer_size=0`、`file_io_advice=off`、`preallocate=off`、`final_verify_policy=full`。
- `verified_chunks` 仍为 opt-in，不设为默认。

### 本机验证

- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：130/130 passed。
- 通过：`python3 -m py_compile tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py`
- 通过：focused CTest：

```bash
ctest --test-dir build -R "FileIo|PosixFile|FileTransferOptions|FileDownloadOptions|ControlOptions" --output-on-failure
```

- 结果：31/31 passed。

### 本机 storage bench smoke

- 通过：

```bash
./build/gridflux-storage-bench \
  --path /tmp/gridflux-storage-phase4d-smoke.bin \
  --mode all \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 2 \
  --preallocate off \
  --file-io-advice off
```

- 输出摘要：

```text
storage_bench operation=write ... write_call_count=32 avg_write_bytes_per_call=65536 result=pass
storage_bench operation=read ... read_call_count=32 avg_read_bytes_per_call=65536 result=pass
storage_bench operation=rewrite ... write_call_count=32 avg_write_bytes_per_call=65536 result=pass
```

- 通过 wrapper smoke：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build \
  --bytes 1048576 \
  --modes write,read \
  --preallocates off \
  --file-io-advices off,sequential \
  --buffer-sizes 65536 \
  --iterations 2 \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T180223Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260516T180223Z_storage-bench-summary.csv`
- 结果：12 rows，0 failures。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录未强删。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- <redacted>二全量测试结果：130/130 passed。

### 私网脚本 smoke

- 为验证 Phase 4D 新增 `--file-io-buffer-sizes` / file IO CSV 字段跨机可用，运行一组小型私网 smoke：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1048576 \
  --connections 1 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c \
  --checksum-backend auto \
  --final-verify-policies full \
  --file-io-buffer-sizes 0,1048576 \
  --file-io-advices off \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- Raw CSV：`tools/perf/results/20260516T181048Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260516T181048Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：4/4 pass，全部 sha256 一致。
- 该 smoke 只验证脚本和参数链路；不作为 Phase 4D 1GiB median 性能结论。

### 待性能窗口运行

- 双机 1GiB storage bench：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --file-io-advices off,sequential \
  --buffer-sizes 65536,262144,1048576,4194304 \
  --iterations 3 \
  --output-dir tools/perf/results
```

- 私网 Phase 4D matrix：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --final-verify-policies full,verified_chunks \
  --file-io-buffer-sizes 0,1048576 \
  --file-io-advices off,sequential \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

### 当前判断

- Phase 4D 只准备 file IO backend 边界并优化 POSIX 可观测性，不直接引入 io_uring。
- `file_io_buffer_size=0` 与 `file_io_advice=off` 继续保持默认。
- 后续是否进入 io_uring，应以 Phase 4D median、IO call count、average bytes per call 和 file IO wait time 为依据。
- 当前目录不是 git 仓库，无法提供 `git status`；这与项目早期记录一致，本次仍使用 rsync 同步<redacted>二。
- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。

## 2026-05-17 Phase 4E 重型性能验收与 io_uring 设计闸门

### 实施内容

- 新增 `tools/perf/analyze_phase4e.py`：
  - 输入 storage bench summary CSV、GridFTP private matrix raw/summary CSV。
  - 输出 Markdown gate 报告，包含 median 表、stage/file IO 指标、默认策略建议和 Phase 4F io_uring prototype 范围。
- 新增 `docs/perf/PHASE4E_IO_URING_GATE.md`。
- 更新 `INDEX.md`、`docs/ROADMAP.md`、`docs/perf/README.md`。
- 未实现 io_uring，未改变 STOR/RETR framed data path。
- 默认值保持不变：
  - `file_io_buffer_size=0`
  - `file_io_advice=off`
  - `preallocate=off`
  - `final_verify_policy=full`
  - `verified_chunks` 仍为 opt-in。

### 本机与<redacted>二基础验证

- 通过：`python3 -m py_compile tools/perf/analyze_phase4e.py tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py`
- 通过：本机 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：本机 `cmake --build build`
- 通过：本机 `ctest --test-dir build --output-on-failure`
- 本机全量测试结果：130/130 passed。
- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录未强删。
- 通过：<redacted>二 configure/build/full CTest。
- <redacted>二全量测试结果：130/130 passed。

### 重型 storage bench

- 运行：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --file-io-advices off,sequential,sequential_dontneed \
  --buffer-sizes 65536,262144,1048576,4194304 \
  --iterations 3 \
  --output-dir tools/perf/results
```

- Raw CSV：`tools/perf/results/20260517T043710Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T043710Z_storage-bench-summary.csv`
- 结果：384/384 rows pass。

### 重型 GridFTP-like private matrix

- STOR crc32c：
  - Raw CSV：`tools/perf/results/20260517T050357Z_gridftp-private-matrix-smoke.csv`
  - Summary CSV：`tools/perf/results/20260517T050357Z_gridftp-private-matrix-smoke-summary.csv`
  - 结果：108/108 pass。
- STOR none：
  - Raw CSV：`tools/perf/results/20260517T053411Z_gridftp-private-matrix-smoke.csv`
  - Summary CSV：`tools/perf/results/20260517T053411Z_gridftp-private-matrix-smoke-summary.csv`
  - 结果：108/108 pass。
- RETR crc32c：
  - Raw CSV：`tools/perf/results/20260517T060356Z_gridftp-private-matrix-smoke.csv`
  - Summary CSV：`tools/perf/results/20260517T060356Z_gridftp-private-matrix-smoke-summary.csv`
  - 结果：108/108 pass。
- RETR none：
  - Raw CSV：`tools/perf/results/20260517T063335Z_gridftp-private-matrix-smoke.csv`
  - Summary CSV：`tools/perf/results/20260517T063335Z_gridftp-private-matrix-smoke-summary.csv`
  - 结果：108/108 pass。
- 合计：432/432 1GiB private matrix cases pass，全部 sha256 一致。

### 回归验证

- 通过：

```bash
ctest --test-dir build -R "gridftp.*resume|retr_corrupt|checksum|download|Manifest" --output-on-failure
python3 tools/test/run_gridftp_control_resume_smoke.py --build-dir build
python3 tools/test/run_gridftp_control_retr_resume_smoke.py --build-dir build
python3 tools/test/run_gridftp_control_retr_corrupt_resume_smoke.py --build-dir build
```

- CTest focused result：22/22 passed。
- STOR resume、RETR resume、RETR corrupt resume smoke 均通过。

### Gate 结论

- 报告：`docs/perf/PHASE4E_IO_URING_GATE.md`
- 代表性 baseline（`preallocate=off`、`file_io_buffer_size=0`、`file_io_advice=off`、effective full）：
  - STOR crc32c median：1.078850 Gbps。
  - STOR none median：1.312390 Gbps。
  - RETR crc32c median：3.744850 Gbps。
  - RETR none median：4.327260 Gbps。
- `file_io_buffer_size=1MiB/4MiB` 不满足默认启用门槛。
- `file_io_advice=sequential` 不满足默认启用门槛。
- `file_io_advice=sequential_dontneed` 在 RETR 和 storage read 中明显有害，不推荐用于文件传输。
- `preallocate=full` 不满足默认启用门槛，继续保持 off。
- `verified_chunks` 继续 opt-in，不设为默认。
- 允许 Phase 4F 进入可选 file-IO-only io_uring prototype 设计/实现评审；Phase 4E 不切主路径。

### 最终检查

- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。

## 2026-05-16 Phase 4B 性能瓶颈拆解与低风险优化

### 实施内容

- 新增 `TransferPhaseStats` / `ScopedPhaseTimer`：
  - STOR server、upload client、RETR sender、download client 均输出阶段字段。
  - 字段覆盖 recv/send/read/write/checksum/manifest flush/resume precheck/final verify/rename commit/overall 的 seconds 与 bytes。
  - 成功日志保持单行 `key=value`，旧字段保留。
- 新增 `FinalVerifyPolicy`：
  - CLI 支持 `--final-verify-policy full|verified_chunks`。
  - 默认 `full`，保持 Phase 4A 完整 temp 重读语义。
  - `verified_chunks` 仅在 checksum 非 none、verified chunks 覆盖完整 transfer、missing ranges 为空且 manifest 已 flush 时生效；否则回退 full 并输出 effective policy。
- `DownloadSession` 与 `TransferSession` 对齐 manifest batch flush：
  - 默认每 16 个 verified chunk flush。
  - 失败、resume precheck、commit 前强制 flush。
- 新增 `tools/perf/run_private_host_baseline.py`：
  - 优先 iperf3/fio。
  - 缺工具时使用 GridFlux memory sink 与 Python 顺序 IO fallback。
  - 输出 host/link/disk/checksum baseline CSV 和原始日志。
- 扩展 `tools/perf/run_gridftp_private_matrix.py`：
  - 新增阶段字段、`host_baseline_csv`、`--final-verify-policy`、`--manifest-flush-interval-chunks`。
  - CSV 继续以接收端指标为主：STOR 取 server receiver，RETR 取 download client receiver。
- 文档新增 `docs/perf/PHASE4B_BOTTLENECKS.md`，更新 `INDEX.md`、`docs/ROADMAP.md`、`docs/perf/README.md`。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`python3 -m py_compile tools/perf/run_gridftp_private_matrix.py tools/perf/run_private_host_baseline.py`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：125/125 passed。
- 重点单测通过：
  - `FileTransferOptionsTest`
  - `FileDownloadOptionsTest`
  - `ControlOptionsTest`
  - `DownloadSessionTest`
  - `TransferSessionTest`

### <redacted>二同步与验证

- 首次远端同步误用了临时密码假设，返回 `Permission denied (publickey,password)`；未改动远端。
- 修正为文档中<redacted>二密码后通过：

```bash
GRIDFLUX_SSH_PASSWORD='***' tools/perf/sync_remote.sh \
  --host root@<redacted> \
  --source /root/projects/GridFlux \
  --target /root/projects/GridFlux
```

- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录未强删，源码同步完成。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- <redacted>二全量测试结果：125/125 passed。

### 私网 host/link baseline

- 通过：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_private_host_baseline.py \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T155847Z_host-baseline.csv`
- 结果：7/7 pass。
- 摘要：

```text
network gridflux_memory_sink 19.1192 Gbps
server disk_write python fallback 1.033312 Gbps
server disk_read  python fallback 0.940298 Gbps
client disk_write python fallback 1.031349 Gbps
client disk_read  python fallback 29.511012 Gbps
server checksum crc32c hardware 47.5799 Gbps
client checksum crc32c hardware 47.6092 Gbps
```

### 私网 Phase 4B matrix / samples

- 64MiB STOR/RETR smoke，CRC32C auto：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 67108864 \
  --connections 1,4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c \
  --checksum-backend auto \
  --host-baseline-csv tools/perf/results/20260516T155847Z_host-baseline.csv \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T155955Z_gridftp-private-matrix-smoke.csv`
- 结果：4/4 pass。
- 摘要：STOR 7.93Gbps 左右，RETR 7.94-8.18Gbps。

- 16MiB STOR/RETR resume smoke：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor-resume,retr-resume \
  --bytes 16777216 \
  --connections 4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c \
  --checksum-backend auto \
  --max-chunks 2 \
  --host-baseline-csv tools/perf/results/20260516T155847Z_host-baseline.csv \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T160009Z_gridftp-private-matrix-smoke.csv`
- 结果：2/2 pass。
- 摘要：STOR resume 6.99019Gbps，RETR resume 6.48152Gbps。

- 1GiB STOR/RETR representative，CRC32C auto + none 对照，full final verify：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --final-verify-policy full \
  --host-baseline-csv tools/perf/results/20260516T155847Z_host-baseline.csv \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T160024Z_gridftp-private-matrix-smoke.csv`
- 结果：4/4 pass。
- 摘要：

```text
STOR 1GiB crc32c hardware full 1.44590 Gbps
STOR 1GiB none            full 1.48136 Gbps
RETR 1GiB crc32c hardware full 1.68012 Gbps
RETR 1GiB none            full 5.23149 Gbps
```

- 1GiB RETR opt-in `verified_chunks` final verify 对照：

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c \
  --checksum-backend auto \
  --final-verify-policy verified_chunks \
  --host-baseline-csv tools/perf/results/20260516T155847Z_host-baseline.csv \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T160621Z_gridftp-private-matrix-smoke.csv`
- 结果：1/1 pass，`final_verify_policy_effective=verified_chunks`。
- 摘要：RETR 1GiB crc32c hardware 从 full verify 的 1.68012Gbps 提升到 3.36272Gbps，`stage_final_verify_seconds=0`。

### Phase 4B 瓶颈判断

- 私网 memory sink 约 19.1Gbps，CRC32C hardware 约 47.6Gbps；1GiB framed 文件吞吐 1-5Gbps，说明不应直接把瓶颈归因到网络或 CRC32C 指令。
- 1GiB STOR crc32c 与 checksum none 接近，阶段日志显示主要时间在 temp 文件写入/落盘：`stage_write_seconds` 约 4.9-5.4s；checksum 约 0.19s，不是 STOR 主瓶颈。
- 1GiB RETR crc32c full verify 中 `stage_final_verify_seconds` 约 3.0s，超过 overall 的 20%；opt-in `verified_chunks` 明显改善 RETR，但默认仍保持 `full`。
- `manifest_flush_seconds` 在大文件 RETR 中明显可见，后续需要继续优化 manifest flush 策略或降低提交路径同步压力。
- Phase 4B 结论：暂不引入 io_uring；下一步优先拆解存储路径、manifest flush、final verify 默认策略和测试路径缓存/落盘行为。

### 最终检查

- 通过：本机 full CTest 125/125 passed。
- 通过：<redacted>二 full CTest 125/125 passed。
- 通过：本机和<redacted>二私网 STOR/RETR/resume/1GiB 样本均无 sha256 mismatch。
- 通过：最终同步后再次确认两台<redacted>无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 备注：第一次最终进程检查命令只设置了 `GRIDFLUX_SSH_PASSWORD`，未同步设置 `SSHPASS`，`sshpass -e` 提示环境变量缺失；随后设置 `SSHPASS` 后重试通过。

## 2026-05-16 Phase 4C 存储路径优化、重复采样稳定性、verified_chunks 可靠性硬化

### 实施内容

- 新增 `gridflux-storage-bench`：
  - 使用项目 `PosixFile::readAtAll` / `writeAtAll` 路径。
  - 支持 `write`、`read`、`rewrite`、`all`。
  - 支持 `--preallocate off|full`。
  - 输出单行 `storage_bench key=value`。
- 新增 `tools/benchmark/run_storage_bench.py`：
  - 支持本机、<redacted>二、双端 storage bench。
  - 输出 CSV 和原始日志到 `tools/perf/results/`。
- 新增 storage preallocation 选项：
  - `gridflux-file-server --preallocate off|full`。
  - `gridflux-gridftp-server --preallocate off|full`，用于控制面 STOR temp 文件。
  - `gridflux-file-download-client --preallocate off|full`，用于 RETR download temp 文件。
  - 默认 `off`，保持 Phase 4B 行为；`full` 使用 `posix_fallocate`，失败即返回错误，不静默 fallback。
  - resume 打开已有 temp 时不重新 preallocate，不破坏 manifest/temp 事实源。
- 扩展 `tools/perf/run_gridftp_private_matrix.py`：
  - 新增 `--repeat N`。
  - 新增 `--preallocates off,full`。
  - 新增 `--final-verify-policies full,verified_chunks`。
  - 新增 `--storage-bench-csv <path>`。
  - Raw CSV 新增 `repeat_index`、`preallocate`、`storage_bench_csv`。
  - 新增 summary CSV，按参数分组输出 throughput/elapsed 的 min/median/max 与 pass/fail count。
- 硬化 `verified_chunks`：
  - 默认 `final_verify_policy=full` 不变。
  - checksum `none`、missing ranges、manifest flush 失败均不得进入 `verified_chunks` commit。

### 本机验证

- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：`cmake --build build`
- 通过：`python3 -m py_compile tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py`
- 通过：`ctest --test-dir build --output-on-failure`
- 全量测试结果：126/126 passed。

### 本机 storage bench smoke

- 通过：

```bash
./build/gridflux-storage-bench \
  --path /tmp/gridflux-storage-bench-smoke.bin \
  --mode all \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 1 \
  --preallocate off
```

- 输出摘要：

```text
storage_bench operation=write ... result=pass
storage_bench operation=read ... result=pass
storage_bench operation=rewrite ... result=pass
```

- 通过：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build \
  --bytes 1048576 \
  --modes write,read \
  --preallocates off,full \
  --buffer-sizes 65536 \
  --iterations 1 \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260516T163703Z_storage-bench.csv`
- 结果：4/4 pass。

### 待性能窗口运行

- 双机 1GiB storage bench：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build \
  --remote-build-dir /root/projects/GridFlux/build \
  --bytes 1073741824 \
  --modes write,read \
  --preallocates off,full \
  --buffer-sizes 1048576 \
  --iterations 1 \
  --output-dir tools/perf/results
```

- 已运行，CSV：`tools/perf/results/20260516T164109Z_storage-bench.csv`
- 结果：8/8 pass。
- 摘要：

```text
local write  preallocate=off  1.57415 Gbps
local read   preallocate=off  0.723392 Gbps
local write  preallocate=full 1.26286 Gbps
local read   preallocate=full 76.8183 Gbps
remote write preallocate=off  3.6981 Gbps
remote read  preallocate=off  22.7765 Gbps
remote write preallocate=full 1.01162 Gbps
remote read  preallocate=full 79.7133 Gbps
```

- 私网 repeat=3 1GiB STOR/RETR matrix：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --preallocates off,full \
  --final-verify-policies full,verified_chunks \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

- 已运行，raw CSV：`tools/perf/results/20260516T164246Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260516T164246Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：48/48 pass，全部 sha256 一致。
- 摘要：

```text
STOR crc32c off  full             median 1.405850 Gbps
STOR crc32c off  verified_chunks  median 1.531670 Gbps
STOR crc32c full full             median 1.325350 Gbps
STOR crc32c full verified_chunks  median 1.467300 Gbps
STOR none   off  full             median 1.511650 Gbps
STOR none   off  requested verified_chunks effective full median 1.473280 Gbps
STOR none   full full             median 1.354420 Gbps
STOR none   full requested verified_chunks effective full median 1.472230 Gbps
RETR crc32c off  full             median 3.322600 Gbps
RETR crc32c off  verified_chunks  median 4.250160 Gbps
RETR crc32c full full             median 3.706560 Gbps
RETR crc32c full verified_chunks  median 3.968780 Gbps
RETR none   off  full             median 4.689760 Gbps
RETR none   off  requested verified_chunks effective full median 3.291530 Gbps
RETR none   full full             median 4.088020 Gbps
RETR none   full requested verified_chunks effective full median 3.581370 Gbps
```

### Phase 4C 当前判断

- Phase 4C 默认仍不改变可靠性语义：`preallocate=off`、`final_verify_policy=full`。
- `preallocate=full` 与 `verified_chunks` 都是显式诊断/优化开关。
- 本轮 native storage bench 显示写入路径仍是优先瓶颈；`preallocate=full` 在该 1GiB 样本里未改善写吞吐，因此不能作为默认策略。
- repeat matrix 显示 STOR 仍主要受写入/落盘路径限制；RETR 在 checksum 启用时可受益于 opt-in `verified_chunks`，但 checksum none 请求 `verified_chunks` 已正确回退 full。
- 后续报告必须以 repeat summary 的 median 为主，不以单次最好值作为结论。
- Phase 4C 数据仍不足以证明 epoll/syscall 是主瓶颈；继续不引入 io_uring。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录未强删。
- 通过：<redacted>二 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`
- 通过：<redacted>二 `cmake --build build`
- 通过：<redacted>二 `ctest --test-dir build --output-on-failure`
- <redacted>二全量测试结果：126/126 passed。
- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。

## 2026-05-17 Phase 4F 可选 file-IO-only io_uring prototype

### 实施内容

- 新增 CMake 选项 `GRIDFLUX_ENABLE_IO_URING`，默认 `OFF`。
  - 默认构建不查找、不链接 liburing，定义 `GRIDFLUX_HAS_IO_URING=0`。
  - 显式 `ON` 时查找 `liburing.h` 与 `uring` library。
  - `ON` 但缺依赖时不 fatal，输出 CMake warning，继续编译 unavailable stub。
- 扩展 file IO backend：
  - `FileIoBackendKind` 新增 `IoUring`。
  - parser 支持 `posix|io_uring`，默认仍为 `posix`。
  - 新增 `FileIoContext`，初始化阶段选择 backend，热路径用 concrete switch，不引入虚函数。
  - `FileIoConfig` / `FileIoStats` 兼容 Phase 4D CSV 字段。
- 新增 io_uring 文件 IO 原型源：
  - `src/storage/file_io_uring.cpp`：liburing 可用时提供同步 submit-and-wait regular file read/write 原型。
  - `src/storage/file_io_uring_stub.cpp`：liburing 不可用时返回清晰 unavailable 错误。
  - 范围仅 regular file IO，不处理 socket IO，不改变 network epoll。
- STOR/RETR 文件路径统一使用 `FileIoContext`：upload client source read、STOR server temp write、RETR sender source read、download client temp write。
- CLI/脚本扩展：
  - `gridflux-storage-bench --file-io-backend posix|io_uring`。
  - `tools/benchmark/run_storage_bench.py --file-io-backends posix,io_uring`。
  - `tools/perf/run_gridftp_private_matrix.py --file-io-backends posix,io_uring`。
  - 无 liburing 时显式扫描 `io_uring` 会写入 fail row，不静默跳过。
- 文档更新：`INDEX.md`、`docs/ROADMAP.md`、`docs/perf/README.md`，并新增 `docs/perf/PHASE4F_IO_URING_PROTOTYPE.md`。

### liburing 探测

- 本机：kernel `5.15.0-177-generic`；未发现 `pkg-config liburing`、`/usr/include/liburing.h` 或 `ldconfig` 动态库。
- <redacted>二：kernel `5.15.0-177-generic`；未发现 `pkg-config liburing`、`/usr/include/liburing.h` 或 `ldconfig` 动态库。
- 因两台<redacted>均无 liburing，本阶段不运行 posix/io_uring 1GiB 性能对比；验收重点为默认 POSIX 不受影响和 explicit io_uring 清晰失败。

### 本机验证

- 通过：`python3 -m py_compile tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py`
- 通过：`cmake --build build`
- 通过：`ctest --test-dir build --output-on-failure`
- 结果：`133/133` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因 `GRIDFLUX_HAS_IO_URING=0` skipped。
- 通过：POSIX storage bench 小样本：

```bash
./build/gridflux-storage-bench \
  --path /tmp/gridflux-storage-posix-smoke.bin \
  --mode all \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 1 \
  --preallocate off \
  --file-io-backend posix
```

- 结果：write/read/rewrite 均 `result=pass`，输出包含 `file_io_backend=posix`。

### 本机 io_uring fallback probe

- 通过 configure，且 CMake 输出预期 warning：

```bash
cmake -S . -B build-iouring-probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DGRIDFLUX_ENABLE_IO_URING=ON
```

- Warning 摘要：

```text
GRIDFLUX_ENABLE_IO_URING=ON but liburing was not found; building unavailable backend stub
```

- 通过：`cmake --build build-iouring-probe`
- 通过：`ctest --test-dir build-iouring-probe --output-on-failure`
- 结果：`133/133` passed；io_uring available smoke skipped。
- 通过：显式 io_uring backend unavailable probe：

```bash
./build-iouring-probe/gridflux-storage-bench \
  --path /tmp/gridflux-iouring-unavailable.bin \
  --mode write \
  --bytes 1048576 \
  --buffer-size 65536 \
  --iterations 1 \
  --preallocate off \
  --file-io-backend io_uring
```

- 结果：命令非零退出，输出包含：

```text
result=fail error=file IO backend unavailable: io_uring
```

### 本机脚本 fallback probe

- 运行：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build \
  --bytes 1048576 \
  --modes write \
  --preallocates off \
  --file-io-advices off \
  --file-io-backends posix,io_uring \
  --buffer-sizes 65536 \
  --iterations 1 \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260517T075109Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T075109Z_storage-bench-summary.csv`
- 结果：POSIX rows pass；explicit io_uring rows fail；wrapper 返回非零以提醒操作者，未静默跳过。

### <redacted>二同步与验证结果

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步时仍提示<redacted>二历史残留：`cannot delete non-empty directory: build-private-verify-20260515T163633Z`。该目录未强删。
- 通过：<redacted>二 default configure/build/full CTest：

```bash
cd /root/projects/GridFlux && \
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && \
cmake --build build && \
ctest --test-dir build --output-on-failure
```

- <redacted>二结果：`133/133` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因 `GRIDFLUX_HAS_IO_URING=0` skipped。

### Phase 4F 当前结论

- Phase 4F 完成 optional file-IO-only io_uring prototype 的工程入口、stub fallback、CLI/parser、bench/matrix backend 维度和单元测试。
- 默认仍为 `file_io_backend=posix`。
- 网络仍使用 epoll。
- 未改变 STOR/RETR framed data path、checksum、manifest、resume、final verify 语义。
- 当前两机没有 liburing，因此不做 posix/io_uring 性能对比；后续若安装 liburing，可进入 Phase 4G 做可用路径 storage bench 与私网 STOR/RETR matrix 对比。

### Phase 4F 最终清理

- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 已清理本地 `tools/**/__pycache__` 测试缓存目录。

## 2026-05-17 Phase 4G 真实 liburing 验证与公开发布脱敏闸门

### 实现内容

- 公开发布脱敏：
  - 更新 `.gitignore`，排除本地 `AGENTS.md`、`build*/`、`**/_deps/`、`tools/perf/results/`、日志、临时文件、大文件、`.env*`、key/cert/cookie/token 类文件。
  - 新增 `AGENTS.example.md`，作为公开安全协作模板，不包含真实 IP、密码、token、私钥或 cookie。
  - 新增 `tools/release/check_public_hygiene.py`，扫描私钥块、明文密码/token、`sshpass -p`、`GRIDFLUX_SSH_PASSWORD` 明文赋值、已知私有 IP/密码和服务器登录表。
  - 新增 `tools/release/export_public_repo.py`，导出脱敏公开源码树，排除本地 `AGENTS.md`、build 产物、perf 结果和认证材料，并自动运行 strict hygiene check。
- io_uring 正确性硬化：
  - `src/storage/file_io_uring.cpp` 将 read/write completion loop 抽为可测试 helper。
  - `src/storage/file_io_uring_stub.cpp` 也提供同名 helper，保证无 liburing 构建可测试 completion loop 逻辑。
  - `tests/unit/file_io_test.cpp` 增加 partial completion、retry/EOF/system error 传播测试。
- 未改变默认 backend：`file_io_backend=posix` 仍为默认。
- 未改网络 epoll、STOR/RETR framed data path、checksum、manifest、resume、final verify 语义。

### liburing 环境

- 本机：
  - kernel：`5.15.0-177-generic`
  - OS：Ubuntu 22.04.5 LTS
  - compiler：`g++-13 13.4.0`
  - CMake：`3.22.1`
  - 安装前未发现 liburing；执行 apt 安装 `pkg-config liburing-dev` 后，`pkg-config --modversion liburing` 返回 `2.0`。
- <redacted>二：
  - kernel：`5.15.0-177-generic`
  - OS：Ubuntu 22.04.5 LTS
  - compiler：`g++-13 13.4.0`
  - CMake：`3.22.1`
  - 安装前未发现 liburing；执行 apt 安装 `pkg-config liburing-dev` 后，`pkg-config --modversion liburing` 返回 `2.0`。

### 本机验证

- 通过：`python3 -m py_compile tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py`
- 通过：默认构建：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

- 结果：`135/135` passed。
- 通过：真实 io_uring Release 构建：

```bash
cmake -S . -B build-io-uring-real -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
```

- 结果：`135/135` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 实际运行并通过。
- 通过：真实 io_uring CLI smoke：

```bash
./build-io-uring-real/gridflux-storage-bench \
  --path /tmp/gridflux-iouring-smoke.bin \
  --mode all \
  --bytes 16777216 \
  --buffer-size 262144 \
  --iterations 1 \
  --preallocate off \
  --file-io-backend io_uring
```

- 结果：write/read/rewrite 均 `result=pass`，输出包含 `file_io_backend=io_uring`。

### 公开发布验证

- 运行 `python3 tools/release/check_public_hygiene.py --path .`：
  - 结果：失败，符合预期；私有工作区包含本地 `AGENTS.md` 和历史私网拓扑记录，该命令作为发布闸门提醒不能直接公开工作区。
- 通过公开导出：

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
test ! -f /tmp/gridflux-public/AGENTS.md
test -f /tmp/gridflux-public/AGENTS.example.md
```

- 结果：strict hygiene passed；公开导出目录不包含 `AGENTS.md`，包含 `AGENTS.example.md`。

### <redacted>二同步与验证

- 通过：`tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux`
- 同步脚本已排除本地 `AGENTS.md`、build 产物、perf results 和认证材料。
- <redacted>二历史目录 `build-private-verify-20260515T163633Z` 未删除，仍作为环境残留记录。
- 通过：<redacted>二默认 configure/build/full CTest：

```bash
cd /root/projects/GridFlux && \
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && \
cmake --build build && \
ctest --test-dir build --output-on-failure
```

- 结果：`135/135` passed；默认构建仍为 POSIX/stub，io_uring available smoke skip。
- 通过：<redacted>二真实 io_uring Release build/full CTest：

```bash
cd /root/projects/GridFlux && \
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && \
cmake --build build-io-uring-real && \
ctest --test-dir build-io-uring-real --output-on-failure
```

- 结果：`135/135` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 实际运行并通过。

### 真实 POSIX/io_uring 性能对比

- 运行 storage bench：

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,all \
  --preallocates off,full \
  --file-io-backends posix,io_uring \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results
```

- Raw CSV：`tools/perf/results/20260517T083244Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T083244Z_storage-bench-summary.csv`
- 结果：`cases=320 failures=0`
- 代表性 median：
  - 本机 write 1GiB, 256KiB, preallocate off：POSIX `0.930925 Gbps`，io_uring `0.910293 Gbps`。
  - 本机 read 1GiB, 1MiB, preallocate off：POSIX `78.700300 Gbps`，io_uring `66.775000 Gbps`。
  - <redacted>二 write 1GiB, 1MiB, preallocate off：POSIX `0.931898 Gbps`，io_uring `0.978580 Gbps`。
  - <redacted>二 read 1GiB, 1MiB, preallocate off：POSIX `77.221550 Gbps`，io_uring `65.785750 Gbps`。

- 运行 GridFTP-like private matrix：

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results
```

- Raw CSV：`tools/perf/results/20260517T085311Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T085311Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=24 failures=0`
- 1GiB / 8 connections / 4MiB chunk / 256KiB buffer median：
  - STOR crc32c hardware：POSIX `1.112390 Gbps`，io_uring `1.083810 Gbps`。
  - STOR none：POSIX `1.381770 Gbps`，io_uring `1.411140 Gbps`。
  - RETR crc32c hardware：POSIX `2.888140 Gbps`，io_uring `3.325980 Gbps`。
  - RETR none：POSIX `4.064710 Gbps`，io_uring `3.224280 Gbps`.

### Phase 4G 结论

- 真实 liburing 路径已验证可编译、可运行、可测试、可对比。
- 同步 submit-and-wait io_uring v1 未证明可稳定替代 POSIX 默认：
  - native storage read 明显偏向 POSIX；
  - storage write/rewrite 大体持平；
  - GridFTP STOR 大体持平；
  - GridFTP RETR crc32c 本轮 io_uring 更高，但 checksum none 仍偏向 POSIX。
- 默认继续保持 `file_io_backend=posix`。
- io_uring 继续作为 explicit opt-in backend。
- 如果继续 Phase 4H，应聚焦 file-IO-only queue depth / batching，不改网络 epoll。

### Phase 4G 最终清理

- 通过：`python3 -m py_compile tools/release/check_public_hygiene.py tools/release/export_public_repo.py`
- 通过：公开导出 strict hygiene 复验：

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
test ! -f /tmp/gridflux-public/AGENTS.md
test -f /tmp/gridflux-public/AGENTS.example.md
```

- 通过：重点回归：

```bash
ctest --test-dir build -R "gridftp|resume|checksum|download|Manifest|FileIo|PosixFile" --output-on-failure
```

- 结果：`42/42` passed；默认 build 中 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 预期 skipped。
- 已清理本地 `tools/**/__pycache__`。
- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- Phase 4G 已新增报告：`docs/perf/PHASE4G_IO_URING_REAL_VALIDATION.md`。

## 2026-05-17 Phase 4G-fix + Phase 4H-prep

### 实现内容

- 修复公开发布脱敏闸门：
  - `tools/release/export_public_repo.py` 现在排除任意层级的 `build`、`build-*`、`cmake-build-*`、`out`、`dist`、`.cache`、`Testing`、`CMakeFiles`、`_deps`。
  - export 排除 CMake/Ninja 产物、对象文件、库、依赖文件、ELF 可执行和未知二进制产物。
  - export 输出 `copied_files`、`skipped_files`、`skipped_dirs`、`skipped_build_dirs`，并列出 skipped build dirs。
  - `tools/release/check_public_hygiene.py --strict` 遇到 build-like 目录、CMake/Ninja 产物、未知二进制、ELF 文件或构建二进制产物会失败，不再跳过。
- 新增 `tools/release/test_public_hygiene.py`：
  - 构造临时私有 repo，包含私有 `AGENTS.md`、安全 `AGENTS.example.md`、`build-verify-*` 假二进制和 CMake/Ninja 产物。
  - 验证私有 fixture strict hygiene 失败。
  - 验证 public export 不包含 `AGENTS.md`、不包含 build-like 目录、不包含构建产物，且 strict hygiene 通过。
- `CMakeLists.txt` 注册 `gridflux_release_hygiene`。
- 新增 Phase 4H 设计草案：`docs/perf/PHASE4H_IO_URING_QUEUE_DEPTH_PLAN.md`。
  - 仅设计 queue depth / batching，不实现主路径改动。
  - 明确不做网络 io_uring，不改变默认 POSIX，不改变 checksum/manifest/resume/final verify。

### 发布闸门验证

- 通过：

```bash
python3 -m py_compile tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/release/test_public_hygiene.py
python3 tools/release/test_public_hygiene.py
```

- 通过：

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
test ! -f /tmp/gridflux-public/AGENTS.md
test -f /tmp/gridflux-public/AGENTS.example.md
find /tmp/gridflux-public -type d -name 'build*' -print -quit | grep -q . && exit 1 || true
grep -RIn '<redacted-password>\|<public-ip>\|<private-ip>' /tmp/gridflux-public && exit 1 || true
```

- export summary 摘要：`copied_files=167 skipped_files=1 skipped_dirs=11 skipped_build_dirs=7`。
- 已确认 `/tmp/gridflux-public` 无 `AGENTS.md`，包含 `AGENTS.example.md`，无 `build*` 目录，未检出已知私有 IP/password。

### 回归验证

- 通过：本机默认 Debug build/full CTest。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

- 结果：`136/136` passed；新增 `gridflux_release_hygiene` 通过。
- 通过：本机 `build-io-uring-real` Release/full CTest。

```bash
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- 结果：`136/136` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`，不是 `Skipped`。
- 通过：同步<redacted>二后默认 Debug build/full CTest。

```bash
tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
```

- 结果：`136/136` passed。
- 备注：一次远端 ssh 构建命令因 `sshpass -e` 需要 `SSHPASS` 而非 `GRIDFLUX_SSH_PASSWORD` 未执行成功；随后使用 `SSHPASS=<redacted>` 重跑通过，未打印密码。
- 通过：<redacted>二 `build-io-uring-real` Release/full CTest。

```bash
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'
```

- 结果：`136/136` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`，不是 `Skipped`。
- 已清理本地 `tools/**/__pycache__`。
- 通过：本机无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 通过：<redacted>二无遗留 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- <redacted>二历史目录 `build-private-verify-20260515T163633Z` 未删除。

### Phase 4G-fix 结论

- 公开发布闸门已修复并通过：public export 不再包含 build-like 目录、构建产物、未知二进制、`AGENTS.md` 或已知私有 IP/password。
- 默认 backend 未改变：`file_io_backend=posix`。
- `io_uring` 仍为 explicit opt-in；网络 epoll、STOR/RETR framed data path、checksum、manifest、resume、final verify 语义未改变。
- 下一步进入 Phase 4H：仅设计/评估 file-IO-only io_uring queue depth / batching opt-in prototype。

## 2026-05-17 Phase 4H io_uring queue depth / batching prototype

### 实现内容

- `FileIoConfig` 新增 `queueDepth` 与 `batchSize`，默认均为 `1`；合法范围 `1..256`。
- 新增 CLI 参数并覆盖 file server/client/download client/GridFTP control server/storage bench：
  - `--file-io-queue-depth <N>`
  - `--file-io-batch-size <N>`
- 若显式设置 queue depth 但未设置 batch size，解析后 batch size 跟随 queue depth。POSIX backend 仅记录参数，不改变行为。
- io_uring backend 在单次 regular file `readAtAll` / `writeAtAll` 内按 contiguous range 拆分多个 SQE，并按 `min(batch_size, queue_depth)` 控制提交批次。
- 新增 io_uring stats：submit/wait/completion/SQE/partial/retry count 与 average bytes per SQE；C++ key=value 日志和 CSV 只追加字段，不删除旧字段。
- fake completion helper 覆盖 out-of-order completion、partial completion、EAGAIN retry 和错误传播；真实 liburing smoke 在 `build-io-uring-real` 中运行。
- `tools/benchmark/run_storage_bench.py` 与 `tools/perf/run_gridftp_private_matrix.py` 支持 queue depth / batch size 维度，并生成 raw + summary CSV。
- 不改变默认 backend：`file_io_backend=posix`。不改网络 epoll，不改 STOR/RETR framed data path、checksum、manifest、resume、final verify 语义。

### 本机验证

- 通过：脚本编译检查。

```bash
python3 -m py_compile tools/benchmark/run_storage_bench.py tools/perf/run_gridftp_private_matrix.py tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/release/test_public_hygiene.py
```

- 通过：本机默认 Debug build/full CTest。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

- 结果：`139/139` passed；默认 build 中 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 预期 skipped。
- 通过：本机 `build-io-uring-real` Release/full CTest。

```bash
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- 结果：`139/139` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`，不是 `Skipped`。
- 备注：第一次本机 `build-io-uring-real` full CTest 中 `gridflux_file_transfer_smoke` 出现一次 `connect: Connection refused` 偶发失败；随后单测重跑通过，全量重跑 `139/139` passed。

### Release hygiene

- 通过：公开导出 strict hygiene。

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
```

- 结果：strict hygiene passed；export summary 摘要 `copied_files=168 skipped_files=1 skipped_dirs=11 skipped_build_dirs=7`。

### <redacted>二验证

- 已同步<redacted>二：

```bash
tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
```

- 通过：<redacted>二默认 Debug build/full CTest。

```bash
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
```

- 结果：`139/139` passed；默认 build 中 io_uring real smoke 预期 skipped。
- 通过：<redacted>二 `build-io-uring-real` Release/full CTest。

```bash
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'
```

- 结果：`139/139` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`，不是 `Skipped`。

### 性能采样

- 通过：本机 storage bench queue-depth smoke。

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build-io-uring-real \
  --bytes 67108864 \
  --modes write,read,rewrite \
  --preallocates off \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-advices off \
  --buffer-sizes 262144,1048576 \
  --iterations 3 \
  --output-dir tools/perf/results \
  --timeout 180
```

- Raw CSV：`tools/perf/results/20260517T102821Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T102821Z_storage-bench-summary.csv`
- 结果：`cases=192 failures=0`。
- 代表性 64MiB / 256KiB median：POSIX read `69.529100 Gbps`，io_uring read qd1 `52.034400 Gbps`，io_uring read qd4 `54.313900 Gbps`；POSIX write qd1 `5.944280 Gbps`，io_uring write qd4 `1.169910 Gbps`。

- 通过：私网 GridFTP-like queue-depth smoke。

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 67108864 \
  --connections 4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4 \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 300
```

- Raw CSV：`tools/perf/results/20260517T103037Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T103037Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=16 failures=0`。

- 通过：私网 1GiB sample。

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 600
```

- Raw CSV：`tools/perf/results/20260517T103220Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T103220Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=32 failures=0`。
- 备注：该 1GiB sample 跑完后发现 `run_gridftp_private_matrix.py` 的 RETR download client 未传递 queue/batch 参数，导致 RETR 行仅代表 effective qd=1；已修复脚本，并用下面的 post-fix smoke 验证字段。

- 通过：RETR post-fix queue-depth 字段 smoke。

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions retr \
  --bytes 67108864 \
  --connections 4 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums none \
  --checksum-backend auto \
  --file-io-backends io_uring \
  --file-io-queue-depths 4 \
  --file-io-buffer-sizes 0 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 300
```

- Raw CSV：`tools/perf/results/20260517T104113Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T104113Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=1 failures=0`；CSV 记录 `file_io_queue_depth=4` 与 `file_io_batch_size=4`。

### Phase 4H 结论

- Correctness 与可回退性通过：默认 Debug 与 real io_uring Release 在两台<redacted>均 full CTest 通过。
- public export strict hygiene 继续通过。
- queue depth/batching 参数已从 CLI、storage bench、GridFTP control server、upload/download client 传到 CSV。
- 当前 smoke 和 1GiB repeat=1 sample 未证明 queue depth/batching 可替代 POSIX 默认；`io_uring` 继续 explicit opt-in，默认保持 POSIX。
- 完整 1GiB repeat=3 queue-depth 重型矩阵尚未补跑；下一阶段如继续，应使用修复后的 RETR 参数传递重新采样。
- <redacted>二历史目录 `build-private-verify-20260515T163633Z` 未删除。

## 2026-05-17 Phase 4I io_uring queue-depth heavy gate

### 实现内容

- 修复 `tools/benchmark/run_storage_bench.py --side local` 误触发远端 SSH 的问题：
  - local side 只执行本地 fs snapshot、bench 和 cleanup。
  - remote fs snapshot、remote bench 和 remote cleanup 仅在 `--side remote|both` 时执行。
- storage bench summary CSV 增加 io_uring submit/wait/completion/SQE/partial/retry/avg bytes per SQE 的 min/median/max 聚合字段。
- GridFTP-like private matrix summary CSV 增加同样的 io_uring 聚合字段。
- 新增 `tools/benchmark/test_run_storage_bench.py`，并在 CMake 注册 `gridflux_storage_bench_wrapper_behavior`。
- 新增 `tools/perf/analyze_phase4i.py`，生成 `docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md`。

### 本机验证

- 通过：脚本编译与 wrapper 测试。

```bash
python3 -m py_compile tools/benchmark/run_storage_bench.py tools/benchmark/test_run_storage_bench.py tools/perf/run_gridftp_private_matrix.py tools/perf/analyze_phase4i.py
python3 tools/benchmark/test_run_storage_bench.py --script tools/benchmark/run_storage_bench.py
```

- 通过：`--side local` 搭配不可达 remote 的小样本不会触发 SSH。

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --remote no-such-remote.invalid \
  --build-dir build-io-uring-real \
  --bytes 1048576 \
  --modes write \
  --file-io-backends posix \
  --file-io-queue-depths 1 \
  --buffer-sizes 65536 \
  --preallocates off \
  --iterations 1 \
  --timeout 60
```

- 通过：本机默认 Debug build/full CTest。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

- 结果：`140/140` passed；默认 build 中 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 预期 skipped。
- 通过：本机 `build-io-uring-real` Release/full CTest。

```bash
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- 结果：`140/140` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`。

### <redacted>二验证

- 已同步<redacted>二：

```bash
GRIDFLUX_SSH_PASSWORD=<redacted> tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
```

- 通过：<redacted>二默认 Debug build/full CTest。
- 结果：`140/140` passed；默认 build 中 io_uring real smoke 预期 skipped。
- 通过：<redacted>二 `build-io-uring-real` Release/full CTest。
- 结果：`140/140` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为 `Passed`。

### Phase 4I storage heavy matrix

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,rewrite \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-batch-sizes 1,4,8,16 \
  --buffer-sizes 262144,1048576 \
  --preallocates off \
  --iterations 3 \
  --output-dir tools/perf/results \
  --timeout 1800
```

- Raw CSV：`tools/perf/results/20260517T122107Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T122107Z_storage-bench-summary.csv`
- 结果：`cases=1536 failures=0`；summary groups `384`，total `fail_count=0`。
- Summary 字段已包含 io_uring submit/wait/completion/SQE/partial/retry/avg bytes per SQE 的 min/median/max。

### Phase 4I GridFTP-like private matrix

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix,io_uring \
  --file-io-queue-depths 1,4,8,16 \
  --file-io-buffer-sizes 262144 \
  --file-io-advices off \
  --preallocates off \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

- Raw CSV：`tools/perf/results/20260517T141550Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T141550Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=96 failures=0`；summary groups `32`，total `fail_count=0`。
- sha256 校验：所有 row `source_sha256 == dest_sha256`。
- RETR queue/batch 验证：io_uring RETR rows 的 `file_io_queue_depth` / `file_io_batch_size` 为请求值 `1/4/8/16`，不再是 Phase 4H pre-fix qd=1 假象。

### Phase 4I analysis

```bash
python3 tools/perf/analyze_phase4i.py \
  --storage-summary-csv tools/perf/results/20260517T122107Z_storage-bench-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T141550Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md
```

- 报告：`docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md`
- 代表性 private median：
  - STOR crc32c：POSIX qd=1 `0.995644 Gbps`；io_uring qd=4 `1.294460 Gbps`，但 qd=8/16 波动较大。
  - STOR none：POSIX qd=1 `1.415750 Gbps`；io_uring qd=16 `1.409510 Gbps`，未超越 POSIX。
  - RETR crc32c：POSIX qd=1 `3.815400 Gbps`；io_uring qd=8 `3.783750 Gbps`，未超越 POSIX。
  - RETR none：io_uring qd=1 `4.636490 Gbps` 高于 POSIX qd=1 `4.160300 Gbps`，但 qd=4/8/16 退化且波动较大。
- Gate 结论：repeat=3 样本完整通过，但收益不稳定且未贯穿 STOR/RETR 与 crc32c/none；不继续把 io_uring batching 作为默认路径候选。下一步回到 POSIX storage/writeback、checksum、final verify 路径分析。
- 默认值保持不变：`file_io_backend=posix`，network epoll 不变，`final_verify_policy=full`，`preallocate=off`，`verified_chunks` 仍 opt-in。

### 状态说明

- Phase 4H 1GiB RETR sample 因 download client queue/batch 参数传递问题不作为最终判断；Phase 4I 已用修复后的脚本重跑 repeat=3。
- <redacted>二历史目录 `build-private-verify-20260515T163633Z` 未删除。

## 2026-05-17 Phase 4J POSIX storage/writeback, checksum, final verify diagnosis

### 实现内容

- 新增 `ManifestFlushPolicy` 与 `CommitSyncPolicy`：
  - `--manifest-flush-policy every_n_chunks|final_only`，默认 `every_n_chunks`。
  - `--commit-sync-policy none|fsync_file|fsync_file_and_dir`，默认 `none`。
  - `final_only` 仅用于诊断，失败/commit 前仍强制 flush，不改变 manifest/verified_chunks 恢复事实源。
- STOR/RETR key=value 日志追加阶段语义别名：
  - STOR receiver：`data_receive_seconds`、`temp_write_seconds`、`checksum_seconds`、`manifest_flush_seconds`、`final_verify_seconds`、`finalize_rename_seconds`。
  - RETR sender：`source_read_seconds`、`network_send_seconds`、`checksum_seconds`。
  - RETR receiver/download client：`download_temp_write_seconds`、`manifest_flush_seconds`、`final_verify_seconds`、`finalize_rename_seconds`。
- `tools/perf/run_gridftp_private_matrix.py` 扩展：
  - 新增 manifest flush policy、manifest flush interval list、commit sync policy 矩阵维度。
  - Raw CSV 保留接收端主指标，并新增 `sender_*` / `receiver_*` 双侧阶段、file IO 与 io_uring 指标。
  - Summary CSV 对阶段字段、双侧字段和 io_uring 字段输出 min/median/max。
- 新增 `tools/perf/analyze_phase4j.py`，生成 `docs/perf/PHASE4J_POSIX_PIPELINE_DIAGNOSIS.md`。
- 默认值保持不变：`file_io_backend=posix`、`final_verify_policy=full`、`preallocate=off`、`manifest_flush_policy=every_n_chunks`、`commit_sync_policy=none`。

### 本机验证

```bash
python3 -m py_compile tools/perf/run_gridftp_private_matrix.py tools/perf/analyze_phase4j.py tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/release/test_public_hygiene.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

- 结果：默认 Debug build 通过；CTest `142/142` passed。
- 默认 build 中 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 预期 skipped。

```bash
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- 结果：real io_uring Release build 通过；CTest `142/142` passed；io_uring smoke 为 `Passed`。

### <redacted>二验证

```bash
GRIDFLUX_SSH_PASSWORD=<redacted> tools/perf/sync_remote.sh --host root@<redacted> --source /root/projects/GridFlux --target /root/projects/GridFlux
sshpass -e ssh root@<redacted> 'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
sshpass -e ssh root@<redacted> 'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure'
```

- 结果：<redacted>二默认 Debug build/full CTest `142/142` passed。
- 结果：<redacted>二 `build-io-uring-real` Release/full CTest `142/142` passed；真实 io_uring smoke 在 full CTest 中为 `Passed`。
- <redacted>二历史目录 `build-private-verify-20260515T163633Z` 未删除。

### Public export gate

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
```

- 结果：public export strict hygiene passed。
- Export summary：`copied_files=177 skipped_files=1 skipped_dirs=11 skipped_build_dirs=7`。

### Phase 4J smoke / field validation

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 8388608 \
  --connections 2 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 262144 \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies final_only \
  --manifest-flush-interval-chunks-list 16 \
  --final-verify-policies full \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 300
```

- Raw CSV：`tools/perf/results/20260517T154153Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T154153Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=2 failures=0`。
- 字段抽查：STOR row 包含 `data_receive_seconds` / `temp_write_seconds`；RETR row 包含 `sender_source_read_seconds` 和 `receiver_download_temp_write_seconds`。

### Phase 4J 主私网矩阵

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 262144 \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks,final_only \
  --manifest-flush-interval-chunks-list 16,256 \
  --final-verify-policies full,verified_chunks \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

- Raw CSV：`tools/perf/results/20260517T154238Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T154238Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=96 failures=0`；所有 row sha256 一致。

### POSIX writeback add-on

```bash
python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums none \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144,1048576,4194304 \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies final_only \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

- Raw CSV：`tools/perf/results/20260517T160524Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T160524Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=12 failures=0`；所有 row sha256 一致。

### Phase 4J analysis

```bash
python3 tools/perf/analyze_phase4j.py \
  --matrix-summary-csv tools/perf/results/20260517T154238Z_gridftp-private-matrix-smoke-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T160524Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4J_POSIX_PIPELINE_DIAGNOSIS.md
```

- 报告：`docs/perf/PHASE4J_POSIX_PIPELINE_DIAGNOSIS.md`
- 主结论：
  - STOR 最佳 median 约 `1.45 Gbps`，主要瓶颈为 temp write/writeback；最佳 STOR case 中 temp write 占已测阶段约 `89%`。
  - STOR crc32c 与 none median 接近，checksum 不是当前主瓶颈。
  - RETR 最佳 median 约 `4.58 Gbps`；主要由 receiver download write 与 sender network send 构成。
  - `verified_chunks` 对部分 RETR crc32c case 有 opt-in 收益，但 checksum none 会 effective 回退 `full`，默认仍保持 `final_verify_policy=full`。
  - `manifest_flush_policy=final_only`、较大 interval 和 file IO buffer size 都不满足默认启用条件；保留为诊断/opt-in。

### 状态说明

- Phase 4J 不引入 raw FTP STOR/RETR，不改网络 epoll，不默认启用 io_uring、preallocate full 或 verified_chunks。
- 默认后端继续为 POSIX；io_uring 仍 explicit opt-in。

## 2026-05-17 Phase 4K POSIX temp write/writeback optimization

### 实施内容

- 阅读并延续 Phase 4J 结论：STOR 的主要瓶颈在 temp write/writeback，Phase 4K 只做 POSIX 写入路径诊断和 opt-in 小步实验。
- `FileIoStats` 新增 POSIX write syscall 级指标：
  - `write_call_count`
  - `write_syscall_count`
  - `write_retry_count`
  - `write_short_count`
  - `write_zero_count`
  - `write_total_bytes`
  - `write_avg_bytes_per_call`
  - `write_avg_bytes_per_syscall`
- POSIX backend 的 `writeAtAll` 统一走带统计的 `pwrite` loop；保留 `PosixFile::writeAtAll` 兼容接口。
- 新增 `PosixWriteStrategy { auto, direct, coalesced }`：
  - `auto` 保持既有默认语义：`file_io_buffer_size=0` 直写，`>0` 使用 contiguous coalescing。
  - `direct` 强制绕过 `BufferedFileWriter`，用于 A/B。
  - `coalesced` 强制使用 `BufferedFileWriter`，要求 `file_io_buffer_size > 0`。
- CLI 覆盖：
  - `gridflux-file-server`
  - `gridflux-file-client`
  - `gridflux-file-download-client`
  - `gridflux-gridftp-server`
  - `gridflux-storage-bench`
- `run_storage_bench.py` 增加 `--posix-write-strategies` 与 `--file-io-buffer-sizes`；raw/summary CSV 增加 write syscall 字段。
- `run_gridftp_private_matrix.py` 增加 `--posix-write-strategies`；raw/summary CSV 增加 requested/effective strategy 与 writer syscall 字段，同时保留 sender/receiver 双侧指标。
- 新增 `tools/perf/analyze_phase4k.py`，生成 `docs/perf/PHASE4K_POSIX_WRITEBACK_OPTIMIZATION.md`。

### 本机基础验证

```bash
python3 -m py_compile \
  tools/benchmark/run_storage_bench.py \
  tools/benchmark/test_run_storage_bench.py \
  tools/perf/run_gridftp_private_matrix.py \
  tools/perf/analyze_phase4k.py
python3 tools/benchmark/test_run_storage_bench.py
cmake --build build
./build/gridflux_unit_tests --gtest_filter='FileIoTest.*:FileTransferOptionsTest.*:FileDownloadOptionsTest.*:ControlOptionsTest.*'
ctest --test-dir build --output-on-failure
```

- 结果：通过。
- Targeted unit tests：`34 passed / 1 skipped`，skipped 为默认 Debug build 中未启用 io_uring 的真实 smoke。
- 本机 Debug full CTest：`144/144` passed。

### 本机 real io_uring Release 回归

```bash
cmake -S . -B build-io-uring-real -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real \
  -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable \
  --output-on-failure
```

- 结果：`144/144` passed。
- `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable`：Passed，不是 Skipped。

### <redacted>二同步与回归

```bash
GRIDFLUX_SSH_PASSWORD='***' tools/perf/sync_remote.sh \
  --host root@<redacted> \
  --source /root/projects/GridFlux \
  --target /root/projects/GridFlux
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'
```

- 结果：同步通过。
- <redacted>二 Debug full CTest：`144/144` passed。
- <redacted>二 real io_uring Release full CTest：`144/144` passed。
- <redacted>二 real io_uring smoke：Passed。
- 备注：第一次私网 smoke 因当前 shell 未设置 `GRIDFLUX_SSH_PASSWORD` 失败；第二次误取 AGENTS 表格用户列作为密码失败；随后改为取<redacted>二表格第 6 列密码并同步重建远端后通过。未在命令输出中打印密码。
- <redacted>二历史 `build-private-verify-20260515T163633Z` 仍作为环境残留保留，未删除。

### Phase 4K smoke / field validation

Local storage smoke:

```bash
python3 tools/benchmark/run_storage_bench.py \
  --side local \
  --build-dir build-io-uring-real \
  --bytes 8388608 \
  --modes write,read,rewrite \
  --file-io-backends posix \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-buffer-sizes 0,262144 \
  --buffer-sizes 262144 \
  --preallocates off \
  --iterations 1 \
  --output-dir tools/perf/results
```

- CSV：`tools/perf/results/20260517T164426Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T164426Z_storage-bench-summary.csv`
- 结果：`cases=30 failures=0`。

Private STOR/RETR smoke:

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 8388608 \
  --connections 2 \
  --chunk-sizes 1048576 \
  --buffer-sizes 65536 \
  --checksums crc32c \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144 \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks \
  --manifest-flush-interval-chunks-list 16 \
  --commit-sync-policies none \
  --final-verify-policies full \
  --repeat 1 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 120
```

- CSV：`tools/perf/results/20260517T164709Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T164709Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=10 failures=0`；所有 row sha256 一致。

### Phase 4K storage bench matrix

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/benchmark/run_storage_bench.py \
  --side both \
  --remote root@<redacted> \
  --build-dir build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --bytes 1073741824 \
  --modes write,read,rewrite \
  --file-io-backends posix \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-buffer-sizes 0,262144,1048576 \
  --buffer-sizes 262144,1048576 \
  --preallocates off \
  --iterations 3 \
  --output-dir tools/perf/results
```

- Raw CSV：`tools/perf/results/20260517T164727Z_storage-bench.csv`
- Summary CSV：`tools/perf/results/20260517T164727Z_storage-bench-summary.csv`
- 结果：`cases=384 failures=0`。
- Summary 字段包含 `posix_write_strategy`、`posix_write_strategy_effective`、`write_syscall_count_*`、`write_retry_count_*`、`write_short_count_*`、`write_zero_count_*`、`write_avg_bytes_per_syscall_*`。

### Phase 4K private matrix

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144,1048576 \
  --posix-write-strategies auto,direct,coalesced \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks \
  --manifest-flush-interval-chunks-list 16 \
  --commit-sync-policies none \
  --final-verify-policies full \
  --repeat 3 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

- Raw CSV：`tools/perf/results/20260517T171606Z_gridftp-private-matrix-smoke.csv`
- Summary CSV：`tools/perf/results/20260517T171606Z_gridftp-private-matrix-smoke-summary.csv`
- 结果：`cases=96 failures=0`；所有 row sha256 一致。
- Summary 字段包含 `posix_write_strategy`、`posix_write_strategy_effective`、`stage_write_*`、`write_syscall_count_*`、`write_avg_bytes_per_syscall_*`，并继续保留 `sender_*` / `receiver_*` 双侧字段。

### Phase 4K analysis

```bash
python3 tools/perf/analyze_phase4k.py \
  --storage-summary-csv tools/perf/results/20260517T164727Z_storage-bench-summary.csv \
  --matrix-summary-csv tools/perf/results/20260517T171606Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4K_POSIX_WRITEBACK_OPTIMIZATION.md
```

- 报告：`docs/perf/PHASE4K_POSIX_WRITEBACK_OPTIMIZATION.md`
- 主结论：
  - Storage bench 与 GridFTP-like private matrix 全部零失败。
  - 没有 POSIX write strategy 满足未来默认启用门槛。
  - STOR `crc32c` 下 256KiB coalescing 有局部约 `+10%` median 提升，但 `checksum=none` 下同族策略退化，未贯穿 STOR/RETR。
  - RETR `crc32c` 出现若干 opt-in 提升，但 `checksum=none` 下经常退化且 min/max 波动仍大。
  - 默认继续 `posix_write_strategy=auto` 且 `file_io_buffer_size=0`。
  - `direct` / `coalesced` 只作为 opt-in 诊断策略保留。

### Release gate / cleanup

```bash
python3 -m py_compile \
  tools/benchmark/run_storage_bench.py \
  tools/perf/run_gridftp_private_matrix.py \
  tools/perf/analyze_phase4k.py
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
ps -eo pid=,args= | grep -E '[g]ridflux-(gridftp-server|file-)' || true
SSHPASS=<redacted> sshpass -e ssh root@<redacted> \
  "ps -eo pid=,args= | grep -E '[g]ridflux-(gridftp-server|file-)' || true"
```

- 结果：py_compile 通过。
- Public export strict hygiene：passed。
- Export summary：`copied_files=180 skipped_files=1 skipped_dirs=11 skipped_build_dirs=7`。
- 本机最终无 `gridflux-gridftp-server` / `gridflux-file-*` 残留进程。
- <redacted>二最终无 `gridflux-gridftp-server` / `gridflux-file-*` 残留进程。

### 状态说明

- Phase 4K 不引入 raw FTP STOR/RETR，不改网络 epoll，不默认启用 io_uring、preallocate full、verified_chunks、final_only 或 commit fsync。
- 默认仍为 `file_io_backend=posix`、`preallocate=off`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`commit_sync_policy=none`。
- POSIX write strategy 默认仍为 `auto`，并保持 `file_io_buffer_size=0`。

## 2026-05-18 Phase 4L performance stability, RETR breakdown, and opt-in recommendation matrix

### Implementation

- Extended `tools/perf/run_gridftp_private_matrix.py` summary output with repeat stability fields:
  - `*_spread_pct` using `(max-min)/median*100`.
  - approximate nearest-rank `*_p95`.
  - `unstable_spread_gt_20pct`, `unstable_minmax_outlier`, `stage_throughput_mismatch`, and `repeat_count`.
- Added per-case before/after environment sidecars for both server and client:
  - `server_env_before_log`, `server_env_after_log`, `client_env_before_log`, `client_env_after_log`.
  - Sidecars capture `free -m`, Dirty/Writeback/Cached from `/proc/meminfo`, `df -h`, and `iostat -xz 1 1` when available; otherwise they record `iostat=unavailable`.
- Added `tools/perf/analyze_phase4l.py`.
  - Reads one or more private matrix summary CSVs.
  - Generates `docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md` with STOR bottleneck rows, RETR sender/receiver breakdown, high-variance rows, opt-in recommendation matrix, and gate conclusion.
  - RETR stage percentages are reported as shares of listed key stages because sender/receiver stage times can be connection-accumulated and cross-side rather than wall-clock percentages.
- Updated `INDEX.md`, `docs/ROADMAP.md`, `docs/perf/README.md`, and `docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md`.

### Local validation

```bash
python3 -m py_compile tools/perf/run_gridftp_private_matrix.py tools/perf/analyze_phase4l.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- `py_compile`: passed.
- Default Debug configure/build: passed.
- Default Debug full CTest: `144/144` passed.
- `build-io-uring-real` Release configure/build: passed.
- `build-io-uring-real` Release full CTest: `144/144` passed.
- Real io_uring smoke: `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed.
- Note: one earlier local Debug CTest was run concurrently with Release CTest and hit a fixed-port `gridflux_file_transfer_smoke` `Connection refused`; the sequential rerun above passed `144/144`.

### Remote validation

```bash
GRIDFLUX_SSH_PASSWORD='***' SSHPASS='***' tools/perf/sync_remote.sh \
  --host root@<redacted> \
  --source /root/projects/GridFlux \
  --target /root/projects/GridFlux

SSHPASS='***' sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'

SSHPASS='***' sshpass -e ssh root@<redacted> \
  'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'
```

- Sync to machine two: passed.
- Machine two default Debug configure/build/full CTest: `144/144` passed.
- Machine two `build-io-uring-real` Release configure/build/full CTest: `144/144` passed.
- Machine two real io_uring smoke: passed.
- Note: one earlier remote Release CTest was run concurrently with Debug CTest and hit the same fixed-port `gridflux_file_transfer_smoke` `Connection refused`; the sequential rerun above passed `144/144`.

### Phase 4L private matrix

```bash
GRIDFLUX_SSH_PASSWORD='***' SSHPASS='***' python3 tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c,none \
  --checksum-backend auto \
  --file-io-backends posix \
  --file-io-buffer-sizes 0,262144 \
  --posix-write-strategies auto,coalesced \
  --file-io-advices off \
  --preallocates off \
  --manifest-flush-policies every_n_chunks,final_only \
  --manifest-flush-interval-chunks-list 16 \
  --commit-sync-policies none \
  --final-verify-policies full,verified_chunks \
  --repeat 5 \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/GridFlux/build-io-uring-real \
  --remote-build-dir /root/projects/GridFlux/build-io-uring-real \
  --output-dir tools/perf/results \
  --case-timeout 900
```

- Raw CSV: `tools/perf/results/20260518T004459Z_gridftp-private-matrix-smoke.csv`
- Summary CSV: `tools/perf/results/20260518T004459Z_gridftp-private-matrix-smoke-summary.csv`
- Result: `cases=240 failures=0`; raw CSV has `240` rows, `0` sha256 mismatches, and all four sidecar env log paths populated for every row.
- Summary: `48` grouped rows, grouped `fail_count=0`, `21` rows with `unstable_spread_gt_20pct=1`.
- The generator skipped invalid `coalesced + file_io_buffer_size=0` combinations.

### Phase 4L analysis

```bash
python3 tools/perf/analyze_phase4l.py \
  --matrix-summary-csv tools/perf/results/20260518T004459Z_gridftp-private-matrix-smoke-summary.csv \
  --output docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md
```

- Report: `docs/perf/PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md`.
- Main conclusions:
  - STOR remains dominated by temp write/writeback. Many STOR rows show temp write around `90%+` of wall time; the default-like STOR crc32c/full/every_n_chunks/auto/fiobuf=0 row had median `1.000700 Gbps` with `48.464675%` spread.
  - RETR is faster but still unstable. The default-like RETR crc32c/full/every_n_chunks/auto/fiobuf=0 row had median `3.323950 Gbps` with `16.588396%` spread.
  - RETR focus changes by row: every_n_chunks rows often lean toward sender network send, while final_only rows often lean toward receiver download temp write.
  - `21/48` summary rows exceed `20%` spread, so Phase 4L does not recommend changing defaults or promoting a strong opt-in combination.
  - Defaults remain: `file_io_backend=posix`, `posix_write_strategy=auto`, `file_io_buffer_size=0`, `preallocate=off`, `final_verify_policy=full`, `manifest_flush_policy=every_n_chunks`, `commit_sync_policy=none`.

### Status notes

- Phase 4L does not introduce a new transfer protocol.
- Phase 4L does not change network epoll, raw FTP STOR/RETR boundaries, checksum, manifest, resume, or final verify semantics.
- `verified_chunks`, `final_only`, `coalesced`, preallocate full, commit fsync, and io_uring remain explicit opt-in / diagnostic choices only.

## 2026-05-18 Phase 4M alpha release gate and stability convergence

### Implementation

- Added `tools/release/run_alpha_release_gate.py`.
  - `--quick` runs existing build, default CTest, io_uring CTest, public export hygiene, STOR/RETR smoke, STOR/RETR resume smoke, metadata/list smoke, and residual process checks.
  - `--full` adds a 1GiB repeat=3 private baseline matrix for STOR/RETR with `crc32c,none` and `full,verified_chunks`, while keeping defaults otherwise unchanged.
  - Outputs `docs/release/ALPHA_RELEASE_GATE.md` and `tools/perf/results/<timestamp>_alpha-release-gate.json`.
- Added `tools/release/check_remote_artifact_sync.py`.
  - Checks selected docs, JSON, raw/summary CSV, and CSV referenced sidecar logs on local and remote trees by SHA256.
  - Reports missing/mismatch artifacts and does not delete remote files.
- Added `tools/release/test_alpha_release_helpers.py` and registered `gridflux_alpha_release_helper_behavior` in CMake.
- Added release docs:
  - `docs/release/README.md`
  - `docs/release/ALPHA_READINESS.md`
  - generated/placeholder `docs/release/ALPHA_RELEASE_GATE.md`
- Updated `INDEX.md`, `docs/ROADMAP.md`, and `docs/perf/README.md`.

### Default behavior

- Phase 4M does not add transfer performance knobs.
- Defaults remain:
  - `file_io_backend=posix`
  - `posix_write_strategy=auto`
  - `file_io_buffer_size=0`
  - `preallocate=off`
  - `final_verify_policy=full`
  - `manifest_flush_policy=every_n_chunks`
  - `commit_sync_policy=none`
- Network epoll, GridFlux framed STOR/RETR, checksum, manifest, resume, and final verify semantics are unchanged.

### Local validation

```bash
python3 -m py_compile \
  tools/release/run_alpha_release_gate.py \
  tools/release/check_remote_artifact_sync.py \
  tools/release/test_alpha_release_helpers.py

python3 tools/release/test_alpha_release_helpers.py

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-io-uring-real -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
```

- `py_compile`: passed.
- `tools/release/test_alpha_release_helpers.py`: passed.
- Default Debug configure/build: passed.
- Default Debug full CTest: `145/145` passed. The new `gridflux_alpha_release_helper_behavior` CTest passed.
- `build-io-uring-real` Release configure/build: passed.
- `build-io-uring-real` Release full CTest: `145/145` passed.
- Real io_uring smoke: `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed.

### Alpha release gate quick

```bash
python3 tools/release/run_alpha_release_gate.py \
  --quick \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --results-dir tools/perf/results
```

- Local-only quick result: passed.
- Remote artifact-sync quick result: passed.
- Markdown report: `docs/release/ALPHA_RELEASE_GATE.md`.
- Local-only JSON report: `tools/perf/results/20260518T022637Z_alpha-release-gate.json`.
- Remote artifact-sync JSON report: `tools/perf/results/20260518T022940Z_alpha-release-gate.json`.
- Quick gate steps passed:
  - `build_debug`
  - `ctest_debug`
  - `ctest_iouring`
  - `ctest_iouring_smoke`
  - `public_export_hygiene`
  - `stor_smoke`
  - `retr_smoke`
  - `stor_resume_smoke`
  - `retr_resume_smoke`
  - `metadata_smoke`
  - `list_smoke`
- Private baseline was not run in quick mode.
- Artifact sync check in remote quick gate: passed.
- Residual process check: no local or remote GridFlux business processes.

### Public hygiene

```bash
rm -rf /tmp/gridflux-public-phase4m-check
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public-phase4m-check --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public-phase4m-check --strict
```

- Public export strict hygiene: passed.
- Export summary during validation: `copied_files=188 skipped_files=1 skipped_dirs=10 skipped_build_dirs=7`.
- Release docs and scripts were checked for known private IP/password patterns; no matches were found.

### Remote validation

```bash
GRIDFLUX_SSH_PASSWORD='***' SSHPASS='***' tools/perf/sync_remote.sh \
  --host <remote> \
  --source /root/projects/GridFlux \
  --target /root/projects/GridFlux

SSHPASS='***' sshpass -e ssh <remote> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'

SSHPASS='***' sshpass -e ssh <remote> \
  'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'
```

- Sync to machine two: passed.
- Machine two default Debug configure/build/full CTest: `145/145` passed.
- Machine two `build-io-uring-real` Release configure/build/full CTest: `145/145` passed.
- Machine two real io_uring smoke: passed.

### Remaining full validation

```bash
GRIDFLUX_SSH_PASSWORD='***' SSHPASS='***' python3 tools/release/run_alpha_release_gate.py \
  --full \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

- Full gate result: passed.
- Markdown report: `docs/release/ALPHA_RELEASE_GATE.md`.
- JSON report: `tools/perf/results/20260518T023057Z_alpha-release-gate.json`.
- Private raw CSV: `tools/perf/results/20260518T023115Z_gridftp-private-matrix-smoke.csv`.
- Private summary CSV: `tools/perf/results/20260518T023115Z_gridftp-private-matrix-smoke-summary.csv`.
- Private matrix: `24` rows, `24` pass, `0` sha256 mismatches.
- Summary: `8` rows, `fail_count=0`, `2` default baseline rows.
- Default baseline rows:
  - STOR crc32c/full/defaults median throughput `0.919468 Gbps`, spread `19.548261%`.
  - RETR crc32c/full/defaults median throughput `3.395120 Gbps`, spread `40.870720%`.
- Remote artifact sync check: passed, `152` artifacts checked, including raw/summary CSV and sidecar logs.
- Final residual process check: no local or remote `gridflux-gridftp-server` / `gridflux-file-*` processes.

### 2026-05-18 Phase 5B release artifact manifest freshness fix

- Architecture acceptance found that `tools/perf/results/20260518T073545Z_alpha-artifacts.json` still recorded the old `docs/PROJECT_STATE.md` hash after the final state document update.
- The stale-manifest strict check reproduced as expected:
  - `docs/PROJECT_STATE.md`: mismatch;
  - `docs/release/ALPHA_RELEASE_GATE.md`: mismatch.
- Fix procedure:
  - update this status record first;
  - regenerate the same artifact manifest path from the final file contents;
  - sync the manifest-listed artifacts to machine two;
  - rerun strict `check_remote_artifact_sync.py --manifest tools/perf/results/20260518T073545Z_alpha-artifacts.json`.
- No transfer code, defaults, checksum, manifest, resume, or final verify semantics changed.

### 2026-05-18 Remote SSH/sync reliability fix

- Added `tools/release/remote_auth.py` as a shared remote-auth helper for release and sync tools.
- The helper uses `GRIDFLUX_SSH_PASSWORD` / `SSHPASS` when present; otherwise it can read the local private `AGENTS.md` topology table and inject the password through `sshpass -e` without printing or storing the secret.
- Updated `tools/perf/sync_remote.sh`, `tools/release/sync_remote_artifacts.py`, `tools/release/check_remote_artifact_sync.py`, and `tools/release/run_alpha_release_gate.py` to use the shared helper.
- Added release helper regression coverage for selecting the correct AGENTS row by remote host and username.
- Verified that `tools/perf/sync_remote.sh` works with `GRIDFLUX_SSH_PASSWORD` and `SSHPASS` unset.
- After this tool change, the artifact manifest must be regenerated because the release tool hashes changed.

### Alpha readiness conclusion

- Alpha status: passed for demonstrable GridFTP-like framed STOR/RETR, bidirectional resume, CRC32C chunk verification, control metadata, release hygiene, and release artifact sync.
- Not beta/production:
  - private baseline spread remains high, especially RETR default baseline;
  - 100G dedicated-line validation is not complete;
  - TLS/GSI/DCAU and production authentication are not implemented;
  - raw FTP STOR/RETR stream compatibility is intentionally unsupported;
  - directory sync/multi-file production workflow is not implemented.

## 2026-05-18 Phase 4N remote sync closure and alpha release ops hardening

### Implementation

- Added `tools/release/sync_remote_artifacts.py`.
  - Reads `tools/perf/results/<timestamp>_alpha-artifacts.json`.
  - Supports `--verify-only`, `--dry-run`, and `--sync`.
  - Emits JSON summary with `checked`, `synced`, `missing`, `mismatch`, `skipped`, `status`, and per-artifact details.
  - Rejects absolute paths, `..`, `AGENTS.md`, build-like paths, `_deps`, credentials, key/cert/cookie/token/password-like paths, and unknown binary artifacts.
  - Uses `rsync -az --relative` for real remote sync and never uses `--delete`.
- Extended `tools/release/check_remote_artifact_sync.py`.
  - Added `--manifest <path>` verify-only mode.
  - Summary now includes `checked`, `missing`, `mismatch`, and `status`.
  - Existing `--path` / `--csv` behavior remains compatible.
- Extended `tools/release/run_alpha_release_gate.py`.
  - full gate writes `tools/perf/results/<timestamp>_alpha-artifacts.json`.
  - Manifest entries record `path`, `size`, `sha256`, `type`, and `required`.
  - Manifest covers release docs, release scripts, gate JSON, private matrix raw/summary CSV, and CSV sidecar logs.
  - full gate runs artifact sync + verify and records `artifact_manifest`, `artifact_sync_summary`, and `artifact_verify_summary` in gate JSON and Markdown.
  - Added an exclusive alpha gate lock under `tools/perf/results/.alpha-release-gate.lock` so two release gates cannot concurrently rewrite the generated report and invalidate remote artifact hashes.
- Extended `tools/release/test_alpha_release_helpers.py`.
  - Covers manifest path filtering, CSV sidecar inclusion, verify-only missing/mismatch detection, sync repair, and traversal/sensitive path rejection.
  - Registered `gridflux_alpha_artifact_sync_behavior` in CMake.
- Updated `docs/release/README.md`, `docs/release/ALPHA_READINESS.md`, `INDEX.md`, `docs/ROADMAP.md`, and `docs/perf/README.md`.

### Defaults and boundaries

- Phase 4N is release/ops hardening only.
- Transfer defaults remain unchanged:
  - `file_io_backend=posix`
  - `posix_write_strategy=auto`
  - `file_io_buffer_size=0`
  - `preallocate=off`
  - `final_verify_policy=full`
  - `manifest_flush_policy=every_n_chunks`
  - `commit_sync_policy=none`
- Network epoll, GridFlux framed STOR/RETR, checksum, manifest, resume, and final verify semantics are unchanged.
- `AGENTS.md`, passwords, tokens, keys, cookies, build outputs, `_deps`, and private auth materials are not included in artifact manifests or public export.

### Local script validation

```bash
python3 -m py_compile \
  tools/release/run_alpha_release_gate.py \
  tools/release/check_remote_artifact_sync.py \
  tools/release/sync_remote_artifacts.py \
  tools/release/test_alpha_release_helpers.py

python3 tools/release/test_alpha_release_helpers.py

cmake --build build
ctest --test-dir build -R "alpha|release" --output-on-failure
```

- `py_compile`: passed.
- `tools/release/test_alpha_release_helpers.py`: passed.
- Default Debug full CTest: `146/146` passed.
- Release helper CTest subset: `3/3` passed, including `gridflux_alpha_artifact_sync_behavior`.
- `build-io-uring-real` Release full CTest: `146/146` passed.
- Real io_uring smoke: `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed.
- Public export strict hygiene: passed for `/tmp/gridflux-public-phase4n`.
- Local quick alpha gate: passed.
  - JSON: `tools/perf/results/20260518T031105Z_alpha-release-gate.json`.
  - Markdown: `docs/release/ALPHA_RELEASE_GATE.md`.
  - quick mode does not generate an artifact manifest because no private matrix artifacts are produced.
- Residual process check after local validation: no local `gridflux-gridftp-server` / `gridflux-file-*` processes.

### Remote and full gate validation

```bash
cmake --build build
ctest --test-dir build --output-on-failure

cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure

tools/perf/sync_remote.sh --host <remote> --source /root/projects/GridFlux --target /root/projects/GridFlux
sshpass -e ssh <remote> \
  'cd /root/projects/GridFlux && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 && cmake --build build && ctest --test-dir build --output-on-failure'
sshpass -e ssh <remote> \
  'cd /root/projects/GridFlux && cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON && cmake --build build-io-uring-real && ctest --test-dir build-io-uring-real --output-on-failure && ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure'

GRIDFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_gate.py \
  --full \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results

python3 tools/release/sync_remote_artifacts.py \
  --manifest tools/perf/results/20260518T034842Z_alpha-artifacts.json \
  --remote <remote> \
  --local-root /root/projects/GridFlux \
  --remote-root /root/projects/GridFlux \
  --verify-only \
  --json-output tools/perf/results/20260518T034842Z_artifact-verify-manual.json
```

- Machine two default Debug configure/build/full CTest: `146/146` passed.
- Machine two `build-io-uring-real` Release configure/build/full CTest: `146/146` passed.
- Machine two real io_uring smoke: passed.
- Clean full alpha gate: passed.
  - Markdown report: `docs/release/ALPHA_RELEASE_GATE.md`.
  - JSON report: `tools/perf/results/20260518T034842Z_alpha-release-gate.json`.
  - Artifact manifest: `tools/perf/results/20260518T034842Z_alpha-artifacts.json`.
  - Private raw CSV: `tools/perf/results/20260518T034900Z_gridftp-private-matrix-smoke.csv`.
  - Private summary CSV: `tools/perf/results/20260518T034900Z_gridftp-private-matrix-smoke-summary.csv`.
  - Private matrix: `24/24` pass, `fail_count=0`, sha256 matched for all rows.
  - Artifact manifest: `161` listed artifacts; sync/verify checked `162` items including the manifest itself.
  - Artifact sync summary: `checked=162`, `synced=3`, `missing=0`, `mismatch=0`, `status=pass`.
  - Artifact verify summary: `checked=162`, `missing=0`, `mismatch=0`, `status=pass`.
  - Manual `--verify-only` artifact check: `checked=162`, `missing=0`, `mismatch=0`, `status=pass`.
- Public export strict hygiene: passed for `/tmp/gridflux-public-phase4n-final`.
- Final residual process check: no local or remote `gridflux-gridftp-server` / `gridflux-file-*` processes.

### Notes

- An earlier concurrent full gate attempt failed because two release gate processes rewrote `docs/release/ALPHA_RELEASE_GATE.md` while artifact hashes were being verified. The final implementation adds an exclusive release-gate lock and the clean rerun above is the accepted Phase 4N result.
- Phase 4N leaves transfer defaults and reliability semantics unchanged; it only hardens release artifact synchronization and alpha acceptance evidence.

## 2026-05-18 Phase 5A directory transfer alpha implementation

### Implementation

- Added alpha directory transfer support on top of existing framed STOR/RETR.
- New tree manifest and scanner modules:
  - `TreeManifest` records mode, logical root, checksum policy, and per-file relative path, size, mtime, transfer_id, status, and error.
  - Upload manifest path: `<source_dir>.gridflux.tree.upload.manifest`.
  - Download manifest path: `<dest_dir>.gridflux.tree.download.manifest`.
  - `scanLocalTree()` scans regular files, rejects symlinks and unsafe relative paths, and returns stable sorted paths.
- New CLIs:
  - `gridflux-tree-upload-client --source-dir <local_dir> --dest-dir <remote_dir>`.
  - `gridflux-tree-download-client --source-dir <remote_dir> --dest-dir <local_dir>`.
- Directory transfer is file-level orchestration only. Each file still uses existing control `STOR` / `RETR`, `REST GFID`, per-file manifest, CRC32C, and final verify logic.
- Control STOR path resolver now creates missing parent directories inside server `--root`; root escape and symlink escape remain rejected.
- Added loopback tree upload/download/resume/corrupt-manifest smokes and a private tree helper.

### Validation

```bash
cmake --build build --target gridflux_unit_tests gridflux-tree-upload-client gridflux-tree-download-client
ctest --test-dir build -R "Tree|ControlOptions" --output-on-failure
python3 tools/test/run_gridftp_tree_upload_smoke.py --build-dir build
python3 tools/test/run_gridftp_tree_download_smoke.py --build-dir build
python3 tools/test/run_gridftp_tree_resume_smoke.py --build-dir build
python3 tools/test/run_gridftp_tree_manifest_corrupt_smoke.py --build-dir build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
python3 tools/release/run_alpha_release_gate.py --quick --build-dir build --io-uring-build-dir build-io-uring-real --results-dir tools/perf/results
python3 tools/test/run_gridftp_tree_private_once.py --remote <remote> --server-host <server-host> --local-build-dir /root/projects/GridFlux/build --remote-build-dir /root/projects/GridFlux/build --connections 2 --output-dir tools/perf/results
```

- Targeted tree/control unit tests: passed.
- Loopback tree upload smoke: passed, 4 files, 1,179,671 bytes, tree hash matched.
- Loopback tree download smoke: passed, 4 files, 1,179,671 bytes, tree hash matched.
- Loopback tree resume smoke: passed for upload and download.
- Corrupt tree manifest smoke: passed; resume failed safely.
- Local default Debug full CTest: `160/160` passed.
- Local `build-io-uring-real` Release full CTest: `160/160` passed; `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped.
- Machine two default Debug full CTest after sync: `160/160` passed.
- Machine two `build-io-uring-real` Release full CTest after sync: `160/160` passed; real io_uring smoke passed.
- Public export strict hygiene: passed for `/tmp/gridflux-public-phase5a`.
- Quick alpha release gate: passed.
  - JSON: `tools/perf/results/20260518T050140Z_alpha-release-gate.json`.
- Full alpha release gate: passed.
  - JSON: `tools/perf/results/20260518T052241Z_alpha-release-gate.json`.
  - Artifact manifest: `tools/perf/results/20260518T052241Z_alpha-artifacts.json`.
  - Private baseline summary: 8 rows, `fail_count=0`, status `pass`; raw CSV `tools/perf/results/20260518T052304Z_gridftp-private-matrix-smoke.csv`, summary CSV `tools/perf/results/20260518T052304Z_gridftp-private-matrix-smoke-summary.csv`.
  - Artifact sync summary: `checked=171`, `synced=3`, `missing=0`, `mismatch=0`, `status=pass`.
  - Artifact verify summary: `checked=171`, `missing=0`, `mismatch=0`, `status=pass`.
  - Final post-doc artifact sync after this status update: `checked=171`, `synced=2`, `missing=0`, `mismatch=0`, `status=pass`.
- Private tree smoke: passed.
  - JSON: `tools/perf/results/20260518T050446Z_gridftp-tree-private.json`.
  - File count: `4`.
  - Total bytes: `1,179,670`.
  - Tree hash: `fcc6ed5a7de263a23097b5ee20519f093781f5601cf462827fae5d3606e3afdb`.
  - Upload, download, upload resume, and download resume tree hashes matched.
- Final residual process check: no local or remote `gridflux-gridftp-server` / `gridflux-file-*` processes.

### Defaults and boundaries

- Defaults unchanged: POSIX file IO backend, `final_verify_policy=full`, `manifest_flush_policy=every_n_chunks`, `preallocate=off`, `posix_write_strategy=auto`.
- Directory transfer remains alpha:
  - does not preserve permissions, owner/group, xattrs, ACLs, or empty directories;
  - does not implement raw FTP recursive transfer, MLST/MLSD, TLS/GSI, production auth, or third-party server-to-server transfer;
  - does not publish or sync private `AGENTS.md`, passwords, tokens, or private topology.

## 2026-05-18 Phase 5B directory transfer concurrency and changed-file hardening

### Implementation

- `gridflux-tree-upload-client` and `gridflux-tree-download-client` now use a bounded file-level scheduler for `--file-parallelism`.
- Each worker opens an independent GridFTP-like control session and still delegates file content to the existing single-file framed STOR/RETR path.
- Tree manifest updates are serialized and atomically saved on `Transferring`, `Completed`, `Failed`, and `Changed` transitions.
- Resume now runs a tree-level changed-file preflight before dispatching new file tasks:
  - upload compares local source size/mtime with manifest records;
  - download compares remote `SIZE`/`MDTM` and completed local target size/mtime with manifest records.
- Download completion aligns local file mtime with the remote manifest mtime so later completed-file validation is meaningful.
- New loopback smokes:
  - `gridflux_tree_parallel_smoke`;
  - `gridflux_tree_changed_file_smoke`.
- New private dataset matrix tooling:
  - `tools/perf/run_gridftp_tree_private_matrix.py`;
  - `tools/perf/analyze_phase5b.py`;
  - `docs/perf/PHASE5B_TREE_DATASET_MATRIX.md`.

### Validation

```bash
cmake --build build --target gridflux-tree-upload-client gridflux-tree-download-client gridflux_unit_tests
python3 -m py_compile tools/test/run_gridftp_tree_parallel_smoke.py tools/test/run_gridftp_tree_changed_file_smoke.py tools/perf/run_gridftp_tree_private_matrix.py tools/perf/analyze_phase5b.py tools/release/run_alpha_release_gate.py
ctest --test-dir build -R "Tree|gridflux_tree" --output-on-failure
ctest --test-dir build --output-on-failure
ctest --test-dir build-io-uring-real --output-on-failure
ctest --test-dir build-io-uring-real -R FileIoTest.IoUringContextReadWriteSmokeWhenAvailable --output-on-failure
python3 tools/test/run_gridftp_tree_private_once.py --remote <remote> --server-host <server-host> --local-build-dir /root/projects/GridFlux/build --remote-build-dir /root/projects/GridFlux/build --connections 2 --output-dir tools/perf/results
python3 tools/perf/run_gridftp_tree_private_matrix.py --remote <remote> --server-host <server-host> --local-build-dir /root/projects/GridFlux/build-io-uring-real --remote-build-dir /root/projects/GridFlux/build-io-uring-real --directions upload,download --datasets mixed --file-parallelisms 1,2,4 --connections 2 --checksums crc32c,none --repeat 3 --output-dir tools/perf/results --case-timeout 900
python3 tools/release/run_alpha_release_gate.py --quick --build-dir build --io-uring-build-dir build-io-uring-real --remote <remote> --remote-root /root/projects/GridFlux --results-dir tools/perf/results
python3 tools/release/run_alpha_release_gate.py --full --build-dir build --io-uring-build-dir build-io-uring-real --remote <remote> --remote-root /root/projects/GridFlux --server-host <server-host> --results-dir tools/perf/results
```

- Tree unit and loopback smoke subset: `17/17` passed.
- Changed-file smoke verifies upload source changes, remote download source changes, and completed local download target changes fail nonzero with the changed relative path and manifest/current metadata.
- Local default Debug full CTest: `163/163` passed.
- Local `build-io-uring-real` Release full CTest: `163/163` passed; `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped.
- Machine two default Debug full CTest after sync: `163/163` passed.
- Machine two `build-io-uring-real` Release full CTest after sync: `163/163` passed; real io_uring smoke passed.
- Private tree smoke: passed.
  - JSON: `tools/perf/results/20260518T071014Z_gridftp-tree-private.json`.
  - File count: `4`.
  - Total bytes: `1,179,670`.
  - Tree hash: `fcc6ed5a7de263a23097b5ee20519f093781f5601cf462827fae5d3606e3afdb`.
  - Upload, download, upload resume, and download resume tree hashes matched.
- Private tree mixed dataset matrix: passed.
  - Raw CSV: `tools/perf/results/20260518T071018Z_gridftp-tree-private-matrix.csv`.
  - Summary CSV: `tools/perf/results/20260518T071018Z_gridftp-tree-private-matrix-summary.csv`.
  - Report: `docs/perf/PHASE5B_TREE_DATASET_MATRIX.md`.
  - Coverage: mixed dataset, upload/download, checksum `crc32c|none`, file parallelism `1|2|4`, repeat `3`.
  - Result: `36/36` pass; `12` summary rows; `fail_count=0`; `tree_hash_mismatch_count=0`.
  - Mixed dataset: `49` files, `79,750,738` bytes.
  - Median throughput:
    - upload crc32c: fp1 `0.286074 Gbps`, fp2 `0.568577 Gbps`, fp4 `1.073540 Gbps`;
    - upload none: fp1 `0.288154 Gbps`, fp2 `0.562555 Gbps`, fp4 `1.088320 Gbps`;
    - download crc32c: fp1 `0.266167 Gbps`, fp2 `0.487340 Gbps`, fp4 `0.833951 Gbps`;
    - download none: fp1 `0.267951 Gbps`, fp2 `0.488943 Gbps`, fp4 `0.829606 Gbps`.
- Quick alpha release gate: passed.
  - JSON: `tools/perf/results/20260518T071313Z_alpha-release-gate.json`.
- Full alpha release gate: passed.
  - JSON: `tools/perf/results/20260518T071402Z_alpha-release-gate.json`.
  - Artifact manifest: `tools/perf/results/20260518T071402Z_alpha-artifacts.json`.
  - Private baseline matrix inside full: `24/24` pass, `fail_count=0`, sha256 matched.
  - Artifact sync summary: `checked=173`, `synced=3`, `missing=0`, `mismatch=0`, `status=pass`.
  - Artifact verify summary: `checked=173`, `missing=0`, `mismatch=0`, `status=pass`.
- Release manifest collection was hardened after the full gate to include Phase 5B tree matrix scripts, analyzer, report, latest tree matrix raw/summary CSV, and CSV-referenced logs. Release helper regression passed.
  - Hardened artifact manifest: `tools/perf/results/20260518T073545Z_alpha-artifacts.json`.
  - Hardened artifact sync summary: `checked=250`, `synced=75`, `missing=0`, `mismatch=0`, `status=pass`.
  - Hardened artifact verify summary: `checked=250`, `missing=0`, `mismatch=0`, `status=pass`.
- Public export strict hygiene: passed for `/tmp/gridflux-public-phase5b`.
- Final public export strict hygiene after artifact hardening: passed for `/tmp/gridflux-public-phase5b-final`.
- Final residual process check: no local or remote `gridflux-gridftp-server` / `gridflux-file-*` processes.

### Defaults and boundaries

- Defaults unchanged: POSIX file IO backend, `final_verify_policy=full`, `manifest_flush_policy=every_n_chunks`, `preallocate=off`, `posix_write_strategy=auto`.
- `--file-parallelism` is now real bounded file-level concurrency and defaults to `1`; per-file data transfer still uses the existing framed STOR/RETR path and `--connections`.
- Directory transfer remains alpha:
  - not rsync;
  - does not preserve permissions, owner/group, xattrs, ACLs, or empty directories;
  - does not implement raw FTP recursive transfer, MLST/MLSD, TLS/GSI, production auth, or third-party server-to-server transfer;
  - does not publish or sync private `AGENTS.md`, passwords, tokens, or private topology.

## 2026-05-18 Phase 5C 目录传输 alpha 硬化完成

### 实现范围

- 新增 tree CLI opt-in JSON summary：`--json-summary <path>`，并支持 `--summary-json` 别名。
- JSON summary 覆盖 direction/source/dest、文件计数、completed/skipped/failed/changed、bytes、file_parallelism、connections、checksum、resume、elapsed、throughput、tree verification hash 和 error 对象。
- Changed-file fail-safe 失败时，JSON error 写入 changed path、manifest/current size 和 mtime。
- 新增 `gridflux_tree_edge_cases_smoke`，覆盖特殊字符路径、深层目录、大量小文件、空目录不保留、symlink 拒绝和 same-size mtime drift fail-safe。
- `run_gridftp_tree_private_matrix.py` 现在为每个 tree CLI case 传入 JSON summary，优先读取 JSON，stdout key=value 仅作为 fallback，并把关键 summary 字段写入 raw/summary CSV。
- `run_alpha_release_gate.py` 增加 artifact manifest freshness check；`sync_remote_artifacts.py` JSON 增加 pre/post sync missing/mismatch 字段。

### 已执行验证

- 通过：`python3 -m py_compile tools/perf/run_gridftp_tree_private_matrix.py tools/release/run_alpha_release_gate.py tools/release/sync_remote_artifacts.py tools/test/run_gridftp_tree_edge_cases_smoke.py tools/perf/analyze_phase5c.py`
- 通过：`cmake --build build --target gridflux-tree-upload-client gridflux-tree-download-client gridflux_unit_tests`
- 通过：`python3 tools/release/test_alpha_release_helpers.py`
- 通过：`ctest --test-dir build -R "TreeTransferOptions|gridflux_tree_edge_cases_smoke|gridflux_alpha_release_helper_behavior" --output-on-failure`，5/5 passed。
- 通过：`ctest --test-dir build -R "gridflux_tree_upload_smoke|gridflux_tree_download_smoke|gridflux_tree_resume_smoke|gridflux_tree_changed_file_smoke|gridflux_tree_edge_cases_smoke" --output-on-failure`，5/5 passed。

### 构建与 CTest 验证

- 本机 Debug full CTest：`164/164` passed。
- 本机 `build-io-uring-real` Release full CTest：`164/164` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped。
- <redacted>二 Debug full CTest after sync：`164/164` passed。
- <redacted>二 `build-io-uring-real` Release full CTest after sync：`164/164` passed；real io_uring smoke passed。

### Phase 5C tree private matrix

- Raw CSV：`tools/perf/results/20260518T084912Z_gridftp-tree-private-matrix.csv`。
- Summary CSV：`tools/perf/results/20260518T084912Z_gridftp-tree-private-matrix-summary.csv`。
- Report：`docs/perf/PHASE5C_TREE_ALPHA_HARDENING.md`。
- Coverage：datasets `small,mixed`，directions `upload,download`，checksum `crc32c|none`，file parallelism `1|2|4`，repeat `3`。
- Result：72/72 pass；24 summary rows；`fail_count=0`；`tree_hash_mismatch_count=0`。
- Mixed dataset：49 files，79,750,738 bytes。
- Small dataset：128 files，524,288 bytes。
- Mixed median throughput:
  - upload crc32c：fp1 `0.289718 Gbps`，fp2 `0.562667 Gbps`，fp4 `1.088710 Gbps`；
  - upload none：fp1 `0.285576 Gbps`，fp2 `0.556227 Gbps`，fp4 `1.088570 Gbps`；
  - download crc32c：fp1 `0.264512 Gbps`，fp2 `0.489268 Gbps`，fp4 `0.826199 Gbps`；
  - download none：fp1 `0.266293 Gbps`，fp2 `0.487775 Gbps`，fp4 `0.843826 Gbps`。

### Release gate and hygiene

- Quick/full alpha release gate are run after final docs are written so the final artifact manifest records final hashes.
- Full gate includes local manifest freshness check before artifact sync/verify; if a required artifact changes after manifest generation, the gate fails with stale paths.
- Public export strict hygiene remains required; local private `AGENTS.md`, passwords, tokens, and private topology are excluded from public export and artifact sync.

### 默认策略

Phase 5C 不改变默认传输策略：`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。

## 2026-05-18 Phase 5D alpha demo / operator experience 收口

### 实现范围

- 新增 deterministic demo dataset generator：`tools/demo/make_demo_dataset.py`。
- 新增 alpha demo runner：`tools/demo/run_alpha_demo.py`，支持 `--mode local|private` 和 `--json-output`。
- Local demo 覆盖 single STOR、single RETR、STOR resume、RETR resume、tree upload、tree download、tree resume、changed-file fail-safe。
- Private demo 复用现有私网 smoke helper，覆盖 private STOR/resume、private RETR/resume、private tree upload/download/resume。
- 新增 operator quickstart：`docs/DEMO.md`。
- 新增 Phase 5D demo report：`docs/release/PHASE5D_ALPHA_DEMO.md`。
- `run_alpha_release_gate.py --quick` 增加 tiny local alpha demo；`--full` 增加 tiny private alpha demo。
- Release artifact manifest now includes `tools/demo/*.py`、`docs/DEMO.md`、Phase 5D report and alpha demo JSON/log artifacts.

### 已执行验证

- 通过：`python3 -m py_compile tools/demo/make_demo_dataset.py tools/demo/run_alpha_demo.py tools/demo/test_alpha_demo.py tools/release/run_alpha_release_gate.py`。
- 通过：`python3 tools/demo/test_alpha_demo.py`。
- 通过：local tiny alpha demo，8/8 cases passed：
  - single STOR/RETR；
  - STOR/RETR resume；
  - tree upload/download/resume；
  - changed-file fail-safe（预期失败路径被正确识别为 demo pass）。
- 通过：private tiny alpha demo，3/3 cases passed：
  - private STOR/resume: 8 MiB；
  - private RETR/resume: 8 MiB；
  - private tree upload/download/resume: 4 files, 1,179,670 bytes。

### 构建与 CTest 验证

- 本机 Debug full CTest：`165/165` passed。
- 本机 `build-io-uring-real` Release full CTest：`165/165` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped。
- <redacted>二 Debug full CTest after sync：`165/165` passed。
- <redacted>二 `build-io-uring-real` Release full CTest after sync：`165/165` passed；real io_uring smoke passed。

### Release gate

- Quick/full alpha release gate are run after Phase 5D docs are written so the final artifact manifest records final required-artifact hashes.
- Full gate still performs local manifest freshness check, artifact sync, and post-sync verify.
- Public export strict hygiene remains required; local private `AGENTS.md`, passwords, tokens, and private topology are excluded from public export and artifact sync.

### 默认策略

Phase 5D 不改变默认传输策略：`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。STOR/RETR 文件数据仍只走 GridFlux framed data channel；demo runner 只编排已有能力，不复制 chunk 级传输逻辑。

## 2026-05-18 Phase 6A security/auth alpha 进行中

### 实现范围

- 新增控制面 auth 配置：`--auth-mode anonymous|token` 和 `--auth-token-file <path>`。
- 默认 `anonymous`，保持现有 `USER gridflux` / `PASS gridflux` 占位认证兼容。
- `token` 模式要求 `USER token` + `PASS <token>`；token 只从权限受限文件读取，空文件、不可读文件和 group/world 可访问文件会被拒绝。
- Protected commands 在未认证时返回 `530`；`FEAT`、`SYST`、`NOOP`、`QUIT`、`USER`、`PASS` 保持未登录可用。
- Tree upload/download clients 新增 `--auth-mode` / `--auth-token-file`，token 模式下内部登录不把 token 写入 JSON summary。
- 新增 `gridflux_gridftp_control_token_smoke` 和 private token auth smoke helper。
- `run_alpha_release_gate.py --quick` 增加本地 token auth smoke；`--full` 增加 private token auth smoke。
- 新增 `docs/SECURITY.md`，明确 Phase 6A 不是 TLS/GSI/生产认证。
- Public hygiene 增加 token leak fixture；token-like artifact path 继续被 release sync 拒绝。

### 已执行验证

- 通过：`python3 -m py_compile tools/test/run_gridftp_control_token_smoke.py tools/test/run_gridftp_control_token_private_once.py tools/test/run_gridftp_control_private_once.py tools/test/run_gridftp_control_retr_private_once.py tools/test/run_gridftp_tree_private_once.py tools/demo/run_alpha_demo.py tools/release/run_alpha_release_gate.py tools/release/check_public_hygiene.py tools/release/test_public_hygiene.py`。
- 通过：`cmake --build build -j2`。
- 通过：`ctest --test-dir build -R "Control(Session|Options)|TreeTransferOptions|gridflux_gridftp_control_token_smoke|release_hygiene" --output-on-failure`，24/24 passed。

### 默认策略

Phase 6A 不改变默认传输策略：`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。Token auth 是 opt-in control-plane alpha，不加密文件数据，不实现 TLS/GSI/DCAU/PROT。

## 2026-05-18 Phase 6B observability and stability alpha 完成

### 实现范围

- 新增轻量 JSONL event log 模块：`gridflux::core::metrics::EventLogger` / `EventRecord`。
- 新增稳定 alpha error code helper，覆盖 `ok`、`auth_required`、`auth_failed`、`path_rejected`、`manifest_corrupt`、`checksum_mismatch`、`changed_file`、`remote_sync_failed`、`io_error`、`protocol_error`、`config_error`、`unknown_error`。
- `gridflux-gridftp-server`、`gridflux-file-client`、`gridflux-file-server`、`gridflux-file-download-client`、`gridflux-tree-upload-client`、`gridflux-tree-download-client` 新增 opt-in `--event-log <path>`。
- Control server 记录 server start、auth success/failure、protected command rejected、STOR/RETR start/complete/fail 和 metadata/list failure。
- Tree JSON summary 增加 top-level `error_code`；changed-file failure 的 error object 也包含 stable error code。
- `tools/demo/run_alpha_demo.py` 支持 `--event-log`，local mode 支持 anonymous/token；demo JSON 增加 `event_summary`、`error_code_counts` 和 `first_error`。
- `tools/release/run_alpha_release_gate.py` 的 step JSON/Markdown 增加 `error_code`、total/passed/failed step count 和 first failed step；full gate 增加短时 local soak smoke。
- 新增 `tools/test/run_gridftp_event_log_smoke.py` 与 `tools/test/run_alpha_soak_smoke.py`，并注册 CTest。
- 新增 `docs/OBSERVABILITY.md`，更新 demo/security/release/perf 文档入口。

### 已执行验证

- 通过：`python3 -m py_compile tools/demo/run_alpha_demo.py tools/test/run_alpha_soak_smoke.py tools/test/run_gridftp_event_log_smoke.py tools/release/run_alpha_release_gate.py tools/release/check_public_hygiene.py tools/release/export_public_repo.py`。
- 通过：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13`。
- 通过：`cmake --build build`。
- 通过 targeted CTest：`ctest --test-dir build -R "EventLog|event_log|alpha_soak|alpha_demo|token|TreeTransferOptions|FileTransferOptions|FileDownloadOptions|ControlOptions" --output-on-failure`，34/34 passed。
- 通过手动 event-log smoke：`python3 tools/test/run_gridftp_event_log_smoke.py --build-dir build`；JSONL 可解析，包含 `auth_required`、`auth_failed`、`ok`，且未包含 token/PASS 明文。
- 通过手动 token soak：`python3 tools/test/run_alpha_soak_smoke.py --build-dir build --iterations 1 --results-dir build/alpha-soak-manual-2 --auth-mode token`。
- 通过本机 Debug full CTest：`175/175` passed。
- 通过本机 `build-io-uring-real` Release full CTest：`175/175` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped。
- 通过<redacted>二 Debug full CTest after sync：`175/175` passed。
- 通过<redacted>二 `build-io-uring-real` Release full CTest after sync：`175/175` passed；real io_uring smoke passed。
- 通过 quick alpha gate：`tools/perf/results/20260518T133832Z_alpha-release-gate.json`，29/29 steps passed。
- 通过 full alpha gate：`tools/perf/results/20260518T140606Z_alpha-release-gate.json`，29/29 steps passed；artifact manifest 为 `tools/perf/results/20260518T140606Z_alpha-artifacts.json`。
- 通过手动 artifact verify：`python3 tools/release/check_remote_artifact_sync.py --manifest tools/perf/results/20260518T140606Z_alpha-artifacts.json`，608 artifacts checked，missing=0，mismatch=0，status=pass。
- 通过 public export strict hygiene：`python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force` 后 `check_public_hygiene.py --strict` passed。
- 完成残留进程检查：本机与<redacted>二均无 `gridflux-gridftp-server` / `gridflux-file-*` 业务进程。
- 发现并修复 release artifact verify 运维瓶颈：`sync_remote_artifacts.py` / `check_remote_artifact_sync.py` 从逐文件 SSH hash 改为单次 SSH 批量 size/sha256 查询；不改变 artifact 安全校验语义。

### 默认策略

Phase 6B 不改变默认传输策略：`auth-mode=anonymous`、`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。Event log 是 opt-in，不实现 TLS/GSI、生产认证、raw FTP stream、100G 优化或 metrics server。

## 2026-05-18 Phase 6C TLS control-plane alpha 完成

### 实现范围

- 新增 opt-in control-plane TLS 配置：`--tls-mode off|explicit|required`、`--tls-cert-file`、`--tls-key-file`、`--tls-ca-file`。
- 默认 `--tls-mode off`；`explicit` 作为未来 AUTH TLS 设计保留并在 Phase 6C 拒绝启动。
- CMake 默认探测 OpenSSL；有 OpenSSL 时编译真实 TLS backend，无 OpenSSL 时默认非 TLS 构建仍可用，显式 TLS 返回清晰配置错误。
- `gridflux-gridftp-server` 在 `required` 模式下对控制连接立即执行 TLS handshake；TLS 成功后继续使用现有 FTP-like `USER/PASS/TYPE/EPSV/STOR/RETR/...` 流程。
- Tree upload/download clients 和 Python control helpers 支持 TLS-required 控制连接。
- Token auth 可以叠加 TLS：TLS 握手后仍执行 `USER token` / `PASS <token>`。
- 新增 `tls_required` / `tls_failed` 稳定错误码；event log 可记录 TLS 结果但不记录证书私钥、token 或密码。
- 新增 loopback TLS smoke 与 private TLS metadata smoke，其中 private smoke 由<redacted>二通过 TLS 连接<redacted>一控制面。
- 更新 `docs/SECURITY.md`、`docs/DEMO.md`、`docs/OBSERVABILITY.md`、`docs/release/README.md`，新增 `docs/release/PHASE6C_TLS_ALPHA.md`。

### 已执行验证

- 通过：`python3 -m py_compile tools/test/run_gridftp_control_tls_smoke.py tools/test/run_gridftp_control_tls_private_once.py tools/demo/run_alpha_demo.py tools/release/run_alpha_release_gate.py tools/release/check_public_hygiene.py tools/release/test_public_hygiene.py`。
- 通过：`python3 tools/test/run_gridftp_control_tls_smoke.py --build-dir build`；覆盖 TLS-required 控制面、plaintext failure、metadata、小 STOR/RETR 和日志无私钥泄漏。
- 通过：`python3 tools/demo/run_alpha_demo.py --mode local --build-dir build --profile tiny --tls-mode required ...`；local alpha demo 8 个 case 全部 pass。
- 通过：`python3 tools/test/run_gridftp_control_tls_private_once.py --remote <remote> --server-host <server-host> ...`；远端<redacted>发起 TLS 控制连接并完成 `SIZE` metadata smoke。
- 通过本机 Debug full CTest：`178/178` passed。
- 通过本机 `build-io-uring-real` Release full CTest：`178/178` passed；`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` passed, not skipped。
- <redacted>二已安装 OpenSSL development package，并完成源码同步。
- 通过<redacted>二 Debug full CTest：`178/178` passed。
- 通过<redacted>二 `build-io-uring-real` Release full CTest：`178/178` passed；real io_uring smoke passed。

### 默认策略

Phase 6C 不改变默认传输策略：`auth-mode=anonymous`、`tls-mode=off`、`file_io_backend=posix`、`final_verify_policy=full`、`manifest_flush_policy=every_n_chunks`、`preallocate=off`、`posix_write_strategy=auto`。TLS 是 control-plane-only alpha：passive STOR/RETR framed data channel 和 LIST/NLST metadata data channel 仍保持现有 TCP 行为。Phase 6C 不实现 GSI/DCAU/PROT、AUTH TLS explicit upgrade、raw FTP stream、生产认证或 data-channel encryption。
