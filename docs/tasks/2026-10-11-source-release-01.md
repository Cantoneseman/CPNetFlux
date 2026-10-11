# SOURCE-RELEASE-01：可复现干净源码归档

日期：2026-10-11
输入提交：`d7b45a8bc9a18d4e861ad0e12deeaacfdc70604e`（AGENT-API-01 完成并已 push）
实现分支：`codex/SOURCE-RELEASE-01`
实施目录：深圳 CPNetFlux worktree `/tmp/cpnetflux-runs/SOURCE-RELEASE-01/src`

## 目标与范围

提供技术性源码交付候选包：必须从显式 commit 的 Git tree 生成，要求 HEAD 精确匹配、tracked/untracked worktree 干净、指定 GitHub remote ref SHA 与提交相同；通过现有 exporter 排除私有 AGENTS、凭据/证书、构建目录、perf payload/results，执行 strict hygiene；生成含 commit/ref/tree hash/archive SHA 的 manifest 和可复现 tar.gz。再从解包源码目录做独立 CMake 构建和 CTest，证明包可构建可测。

## 非目标

不发布 GitHub Release、不向用户站点/软件仓库上传、不编译/签名二进制、不实现 Linux 安装器/systemd/服务配置、不进行桌面真实传输或跨主机实验。不选择开源/商业许可证、不宣称公开再分发许可。当前树尚无根 LICENSE；完成的包只能作为内部源码候选，许可证审查/选型另立审批任务。根 CMake 也没有 install() 规则，安装器不是本任务验收。

## 验收标准

- 从干净且已推送的明确 commit 生成源码包；dirty/untracked、commit 与 HEAD 不匹配、remote ref SHA 不匹配、输出覆盖均失败关闭。
- 导出排除 `AGENTS.md`、secret/key/certificate、build/CMake/Ninja产物、`tools/perf/results`，并通过 strict hygiene。
- tar.gz 归档排序、时间戳、owner/gzip metadata 规范化；相同 commit 两次产物 SHA-256 一致；manifest 记录 source commit、remote/ref、tree hash、archive hash、文件数和 license 状态。
- 测试脚本用临时 Git remote 验证成功、确定性、过滤、输出冲突和 dirty拒绝；CTest 调用此门禁。
- 最终包解压到新目录后进行 TLS-enabled 核心 CMake build/full CTest、桌面独立 CMake build/full CTest；测试证据不写进源码包。
- 按显式路径提交、push 到已核实 GitHub remote、回读远端 SHA。

## 允许路径

`tools/release/create_source_release.py`、`tools/release/test_source_release.py`、必要时 `tools/release/export_public_repo.py`/`check_public_hygiene.py`、`CMakeLists.txt`、`docs/release/README.md`、`docs/release/SOURCE_RELEASE_01.md`、本任务文件、`docs/coordination/BOARD.md`。构建、测试、归档只放 `/tmp/cpnetflux-runs/SOURCE-RELEASE-01/` 下，不提交。

## 回滚

只回滚 `codex/SOURCE-RELEASE-01` 本任务提交；不 reset/clean/覆盖深圳权威根目录或其他 worktree。外部 tar.gz、manifest 和 build 输出仅删除任务自己生成且已核验具体路径的文件，不对共享 `/tmp` 或仓库使用宽泛清理。

## 验收记录

- python3 -m py_compile tools/release/create_source_release.py tools/release/test_source_release.py：通过。
- python3 tools/release/test_source_release.py：通过；覆盖确定性、过滤凭据/AGENTS/build/perf 产物、dirty/错误 ref/覆盖/符号链接拒绝。
- ctest --test-dir /tmp/cpnetflux-runs/SOURCE-RELEASE-01/build-core -R 'cpnetflux_source_release_behavior|cpnetflux_release_hygiene' --output-on-failure：2/2 通过。
- TLS-enabled Release 核心构建：通过；设置临时 CPNETFLUX_TEST_TOKEN 后全量 CTest 258/258 通过，1 项 io_uring 能力测试 skip。第一次未设置该变量时，token 和 event-log 两项按测试设计失败，设置变量重跑全绿。
- 桌面 Release 构建和 CTest：3/3 通过。
- 首次打包先由 strict hygiene 拒绝回归夹具中的 PEM marker；夹具改为运行时拼接。随后从解包候选执行全量 CTest，发现 exporter 的 token 文件名规则误删了两个包含 token 的 Python smoke 源码；现在保留已知源代码后缀并继续过滤 token 数据文件，两个回归测试覆盖此边界。
- 限制：无根 LICENSE，manifest 保持 public_release_ready=false；无根 CMake install() 规则，本任务不产安装器。
