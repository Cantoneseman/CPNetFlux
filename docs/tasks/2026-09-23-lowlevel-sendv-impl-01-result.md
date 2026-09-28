# LOWLEVEL-SENDV-IMPL-01：plain DATA vectored write 实现结果

日期：2026-09-23（Asia/Shanghai）
路线/任务：`R2026-09-23.4 / v1`

## 输入、隔离与范围

- 输入提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- 实际 worktree：`C:\\Users\\12563\\AppData\\Local\\Temp\\cpnetflux-lowlevel-sendv-impl-01\\worktree`。
- 分支：`codex/LOWLEVEL-SENDV-IMPL-01`；worktree HEAD 保持输入提交，未提交 Git。
- 共享根目录 `D:\\Project\\CPNetFlux` 未切分支、未写入、未操作其 Git index。实现 worktree 起始干净；任务单要求的文档/源码资料均按指定路径读取。
- 改动仅在任务白名单及窄测试入口：`CMakeLists.txt`、`include/cpnetflux/core/io/framed_data_socket.h`、`src/core/io/framed_data_socket.cpp`、`src/core/io/framed_data_socket_internal.h`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_sender.cpp`、`tests/unit/framed_data_socket_test.cpp`。未改协议、TLS 实现、checksum/resume、目录、manifest、scheduler、io_uring、runner 或默认值。

## 实现内容

- `FramedDataSocket::writeSegments` 增加 plain TCP 两段写入口；内部使用 `sendmsg` 和最多两个 `iovec`，固定 `MSG_NOSIGNAL`。
- partial write 只推进当前 segment 的未发送尾部，跨 header/payload 边界时不会重发已发送字节；EINTR 重试；零写返回 runtime error；其它 errno 沿现有 system error 语义返回；没有复制整个 payload。
- upload `file_transfer_client.cpp` 与 download `file_download_sender.cpp` 的 DATA frame 只有在 plain TCP 且 payload 非空时走该入口。TLS 和空 payload 继续原有 header/payload `writeAll` 路径；SessionInit、ResumeResponse、ChunkComplete、Fin、Complete 等未改。
- 新增真实 `socketpair` 窄测试：拼接字节相等、header-only/payload-only、header partial、跨 iovec partial、payload partial、EINTR、zero write、peer close/EPIPE，并检查 `MSG_NOSIGNAL`。测试通过内部 sendmsg 函数指针注入制造可控 short write/EINTR，不替代真实 client/server smoke。

## 既有 profile 与本次 after 状态

LOWLEVEL-SINGLE-PROFILE-00 的固定输入资料记录：256 MiB、buffer 64 KiB、chunk 1 MiB、plain TCP、checksum none；每次 4,096 DATA frame。1/8 connections 各 3 次均观察到 4,096 个 header 写和 4,096 个 payload 写，short/unresolved 均为 0，目标 hash 全等；loopback elapsed 中位数约 0.28/0.36 s、sys CPU 约 0.12/0.17 s。该资料只证明机制和 profile 前置，不是跨域性能结论。

本实现没有 Linux after build、运行或 trace，因此 after 的 DATA syscall、CPU/GiB、wall/goodput 均为 `NOT_RUN`。预期门是每个非空 plain DATA frame 的发送调用接近一次；没有实际数据不得宣称达标，也不得外推 100 Mbps 或 100G。

## 实际命令、退出码与状态

- `git rev-parse HEAD`：0，`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- `git status --short --branch`：0，显示任务分支及上述白名单改动。
- `git diff --check`：0。Git 仅提示 `CMakeLists.txt` 和公共头在下次 Git 写入时的 LF→CRLF 转换。
- `git diff --cached --quiet`：0，私有 worktree index 为空。
- `git status --porcelain -- src include tests tools CMakeLists.txt`：0，仅显示上述实现/测试改动。
- `git diff --name-only 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 -- src include tests CMakeLists.txt`：1（预期，列出本任务尚未提交的改动）；未发现白名单外源码/测试/CMake 路径。
- `cmake -S . -B build-sendv -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_ENABLE_TLS=ON`：1，Windows worktree 中 `cmake` 命令不存在。
- `cmake --build build-sendv --parallel 2`：1，`cmake` 命令不存在。
- `ctest --test-dir build-sendv --output-on-failure -R 'cpnetflux_file_(transfer|resume|checksum)_smoke'`：1，`ctest` 命令不存在。
- `wsl.exe --list --quiet`：1，WSL 服务返回 `E_ACCESSDENIED`；Docker daemon 查询也失败，不能用本机替代 Linux 构建。
- CMake、CTest、单元测试、file smoke、tree/control reuse、tree resume、changed-file、edge-case、data TLS smoke、strace、before/after A/B、传输、SSH、云端和清理：均 `NOT_RUN`。没有生成任务专属 ext4 after 证据目录。

## 风险、回退与下一步

- 源码级风险集中在 Linux `sendmsg/iovec` 编译、partial write 状态推进及 callsite 选择；这些因 Linux 构建被环境阻塞，尚未由编译器或运行时验证。TLS fallback 和现有协议路径保持源码不变，但 TLS smoke 尚未执行。
- 回退方式是移除本 worktree 的 `writeSegments`、两个 `sendDataFrame` callsite、内部头文件、窄测试及 CMake 注册；未提交所以没有远端部署或历史提交回退动作。
- 04 下一步应先在独立 Linux build 编译并运行 `FramedDataSocketTest.*`，再运行 file transfer/resume/checksum smoke 和 data TLS smoke；随后按固定 256 MiB case 做 1/8 connections 各至少 3 次 before/after `strace -ff -yy`，保存 peer syscall/bytes/short/unresolved、CPU/GiB、wall/goodput 和 hash 到任务专属 ext4 证据目录。若调用数下降但 CPU/GiB/wall 在重复噪声内无收益，按任务门报告“无可测收益”并回退；任何协议 bytes、hash、resume/checksum/TLS 回归都阻止合入。

## 执行限制

本交付没有 Git commit，也没有把 worktree 改动同步到共享根目录。CLI `--output-last-message` 已实际调用并退出码为 1：CLI 报告 `%USERPROFILE%\\.codex\\state_5.sqlite` 只读，随后 app-server 初始化因 `E_ACCESSDENIED` 失败；摘要文件未生成，不伪造其输出。

## 00 总控独立 Linux 验证补充（2026-09-23）

03 角色报告中的“Linux/WSL 不可用”仅描述其 CLI 会话权限；00 随后通过桌面本地 shell 成功启动 `wsl -d Ubuntu-22.04 -u root`，因此按实际证据补跑，不覆盖 03 的原始限制记录。

- 输入代码仍为独立 worktree `codex/LOWLEVEL-SENDV-IMPL-01`，HEAD=`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，未提交；Windows Git worktree 的 `.git` 指针不能由 WSL Linux Git 解析，故 00 在 Windows 记录分支/状态后对源码文件快照归档。归档 SHA-256：`d541882f34603aab82a97078cb1e821cb8030df0cffeb5692cecb099835282e9`。证据根：`/home/sumu/cpnetflux-evidence/LOWLEVEL-SENDV-IMPL-01-root-final-20260923`。
- Release/Ninja：`CPNETFLUX_BUILD_TESTS=ON`、`CPNETFLUX_ENABLE_IO_URING=OFF`、`CPNETFLUX_ENABLE_TLS=ON`；CMake 配置成功，构建 111/111。更新后的真实 `FrameHeader || payload` 测试快照 SHA-256 为 `ce480050b934c50480b35117937a023943fd8e760d86d475c71f679532ebfec3`；`FramedDataSocketTest` 6/6 通过，file transfer/resume/checksum smoke 3/3 通过，tree upload/download/resume/parallel/control-reuse/changed-file/edge-cases/manifest-corrupt 8/8 通过。源码归档里的 shell smoke 首次因 Windows CRLF 失败；只在 ext4 隔离快照把 `.sh` 规范为 LF 后重跑，三项通过；仓库文件未改变。
- 同一 256 MiB 输入、1 MiB chunk、64 KiB buffer、plain TCP、checksum none，1/8 connections 各 3 次；6 个 wall case 和 6 个 client-only strace case 分别比较固定基线二进制与新二进制。24/24 客户端/服务退出码为 0，目标 SHA-256 均为 `545a77fd421938559bd8fff15e56493a4fa27d2c5961a661fdc3f6ad7fa8251f`。
- 每种 connections 的 4,096 DATA frame：基线为每 frame 一个 64-byte header `sendto` 加一个 65,536-byte payload `sendto`；新版本为每 frame 一个 `sendmsg`，两个 iovec 长度分别 64 与 65,536，返回 65,600。数据帧 socket 调用由 8,192 降为 4,096（−50%）；包含会话帧的 peer send 调用中位数由 8,707→4,611（1 connection，−47.0%），8,728→4,632（8 connections，−46.9%）。无短写、目标 hash 全等。
- 未跟踪 CPU/GiB：1 connection system CPU 中位数 0.19→0.14 s/256 MiB，即 0.76→0.56 s/GiB（−26.3%）；8 connections 0.23→0.19，即 0.92→0.76 s/GiB（−17.4%）。wall 中位数 1 connection 0.27→0.21 s，8 connections 0.23→0.25 s。三次样本很小且 loopback wall 有波动；这是 syscall 与本地 CPU 的正向机制证据，尚不能判定跨域吞吐提升或与 GridFTP 接近。数据明细及原始 trace 位于证据根 `ab/`，汇总 `ab/summary.json`、`ab/results-corrected.csv`，文件 hash 清单仍待最终生成。
- 新客户端二进制 SHA-256：`b818ea8f489f76533c55dba55dc4d9e463c6a2a00260c513e087a7ce1b65c830`；基线客户端：`4be0978b01b16c9d21dc6f47d3d73e1755f9b3ea3dee625fdd1af57d4aa04e21`。这两个二进制由同一 WSL 环境的 Release 构建产生，源码归档与配置均已留存。
- 00 的后处理脚本第一次写 CSV 时因新增字段未加入 CSV 列表而退出 1；原始 case 与 trace 均已成功完成且 hash/退出码全为 0。修正列集合后重算摘要，`summary.json` SHA-256=`88c8c612fb7d2ab13554f14c8a866a51220bcc76f1bf7c587969210c98e24167`，A/B 目录 155 个文件均纳入 `ab-sha256-manifest.txt`，清单 SHA-256=`1d47294d93609affbb8a16b936c66f488fc59f44fdebee646a07f193fe105293`。完整关键证据 hash 清单为证据根 `key-evidence-sha256.txt`。
- 00 的验收状态：实现功能/正确性基础门通过，独立质量角色审查仍待完成；底层 sendv 可保留在隔离 worktree，未合并。数据 TLS 专项、完整 CTest 和深圳—上海对照尚未执行；上海空间、进程归属、构建峰值、认证与数据 TLS 条件仍是云端准入阻塞。
