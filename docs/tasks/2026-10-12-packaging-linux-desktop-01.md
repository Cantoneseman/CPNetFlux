# PACKAGING-LINUX-01 / DESKTOP-01 v1
日期：2026-10-12；负责人：00；深圳单人串行、隔离交付，不修改主目录已有 dirty 文件。

## 输入与路线修正
V3 稳定性 c33a51662e3d2cfbdd10127bf2080ae304bda246、AUTH/API/SOURCE 9d5b25d56add0d41e38cb28220a1b9bc54fe283a、Qt API2 eaadeb24490e7e7246766efce94f01a6d41c5b68 分叉，不能分别冒充完整安装输入。先整合已批准能力，旧 SOURCE 包仍只代表当时提交，不作为本轮完整 V3 包。
GitHub origin：git@github.com:Cantoneseman/CPNetFlux.git。部署工具在深圳实现；native worktree 工具绑定 Windows，远端使用 git worktree。
当前未找到这两个 ID 的版本化详细规格，按用户交付目标和已冻结 API 补成下列可执行切片。

## 范围
PACKAGING：整合已有 V3/认证/桌面提交；CMake install/CPack 的 Linux TGZ + DEB；运行时依赖、版本与校验清单；受控非 root 服务与安全配置模板；支持隔离 DESTDIR 验收、重复安装/升级/卸载说明，默认不启用公网服务。
DESKTOP：已实现 Qt API2 壳的运行与安装闭环；修复分页身份、重连及无效 completed 风险；明确真实 adapter 状态，按可证明接口接入，不能用演示结果冒充真实传输完成。补齐关键行为和安装后离屏 UI/agent 验收。

## 非目标
发布公开 GitHub Release、决定许可证、签名/自动更新、改变传输协议、账号系统、跨域性能/长实验、自动读取或生成用户凭据、自动修改当前机器服务、防火墙或用户数据。
真实 adapter 若缺安全凭据注入或核心结构化证据，保持 unsupported 并留独立明确任务，不把其记作已实现。

## 允许路径
CMakeLists.txt、cmake/、packaging/、tools/release/、desktop/、docs/release/、本任务/决策文档和 BOARD。
整合既有已提交核心改动允许触及对应冲突文件，但不得混入根目录 dirty，须记录冲突选择与回归。
构建、包、payload、证据仅 /tmp/cpnetflux-runs/PACKAGING-LINUX-01 或 DESKTOP-01。

## 验收
定向核心认证/V3/源码测试 -> 全量 CTest（临时测试凭据，io_uring capability skip 分报）。
独立 Qt CMake/CTest；安装后 agent API2、非 demo拒绝虚假传输、UI API不兼容/重连/分页行为。
cmake --install --prefix 到隔离目录及 DESTDIR；TGZ/DEB 解包安装后启动/帮助、依赖与无源码路径泄漏；systemd-analyze verify；安全配置未准备时 fail closed。
打包内容无凭据、build/perf payload；严格 hygiene；精确 commit/push、远端 SHA 回读；包 SHA256 和 manifest。
无图形会话只声明 offscreen 验收；发行许可仍需用户选择，不公开发布。

## 回滚
包与服务未自动部署到系统；删除/保留本任务隔离安装目录由调用者决定。实际部署升级前停止本服务、保留配置/状态/数据，回装上一版本，默认卸载只移除二进制不删数据。
源码回滚用明确提交 git revert，保留根目录与其他 worktree。

## PACKAGING-LINUX-01 验收（2026-10-12）
整合无冲突：SOURCE merge f3c0db5，桌面 cherry-pick 41a9fc9/5c17eb8/d7fba6a；未触碰 root dirty。AUTH 历史 CRLF 使用 core.whitespace=cr-at-eol 检查，未全量格式化。
代码与 GitHub 远端 SHA：0ef9f35e1085387b4f64a891de66a90aea935a5b，codex/PACKAGING-LINUX-01，push成功回读一致。
定向38/38；全量294项：293 pass、1 FileIoTest io_uring capability skip、0 fail（84.80秒）。核心 TLS=ON/Release、Qt6.2。6项部署配置行为测试通过，额外验证未提交输入拒绝。
DESTDIR安装、TGZ及DEB解包后命令/API2真实agent拒绝create、socket0600与退出清理通过。CLI客户端现有help返回2并输出用法，未将其误写成成功传输。
strict sanitized source hygiene通过，470文件，无凭据/构建/payload。两包不含源码/worktree路径，依赖由dpkg-shlibdeps生成，无maintainer scripts、无自动用户/服务操作。
原unit在未安装宿主机校验因ExecStart不存在而失败；在隔离安装路径替换ExecStart后systemd-analyze verify exit0。仅宿主机已有snapd RestartMode警告；未启用任何unit。
证据：/tmp/cpnetflux-runs/PACKAGING-LINUX-01/{build.log,targeted-tests.log,full-ctest.log,install.log,unit-verify.log,artifacts/linux-package-manifest.json}。
包（内部候选，未公开发布）：artifacts/cpnetflux-0.1.0+git.0ef9f35e1085-x86_64.tar.gz
SHA256 ee356cd848812a9773b6c9f63bbf6ae32c4e918fe1e92c7b02592d168aefcef2；
artifacts/cpnetflux_0.1.0+git.0ef9f35e1085_amd64.deb
SHA256 c982ad9a228bcb3ed8b79ae43519a2821f612e1071ecfaecb216320d7b0f533b。
限制：仅Ubuntu22.04/x86_64现场验证、动态系统库依赖；桌面真实adapter未接入；无LICENSE，不是公开发行。下一阶段DESKTOP-01修复UI行为，在独立worktree串行执行并更新最终安装包。

## DESKTOP-01 实现与验收准备（2026-10-12）

独立 worktree：/tmp/cpnetflux-runs/DESKTOP-01/src；分支 codex/DESKTOP-01；输入 7bfaeac91922fbc17f6e36886d78dbf7c36d1c68。
本阶段是 Linux Qt/API2 壳与安装闭环，不是完整真实双机 GUI。剩余产品能力明确拆为 DESKTOP-SECRETSTORE-01、DESKTOP-ADAPTER-01、DESKTOP-LIVE-ACCEPT-01，均 pending；不向其他聊天派单。

行为回归先红后绿：初始四项暴露 refresh/epoch reset不收敛、独立实体snapshot被丢弃、旧请求污染详情、gap reset不收敛；修复后通过。完成闭合增加 files_skipped。自审新增两项，在旧逻辑上分别失败于伪造completed被接受、transfer.snapshot忽略；修复后八项C++场景通过。
新增完成证据校验拒绝error、负数/非整数/超出JSON安全整数、checksum或TLS降级；事件envelope身份须匹配payload，attempt编号不变不得更换ID。UI显示“完成（含跳过）”。
独立Qt四项CTest 4/4通过（11.32秒）；API不兼容与离线smoke返回3，agent同步后才返回0。无物理图形会话，不宣称用户交互或真实传输验收。
整合构建/安装已验收；最终包结果待下方补齐。

离屏软件平台实际截图请求没有输出PNG；宿主机无Xvfb，不补装系统软件。修正smoke：截图保存失败显式exit4；七页面门只声称QML加载/同步、无binding错误，不声称像素或实际交互验收。

## DESKTOP-01 代码门（2026-10-12）

Release/TLS=ON/desktop=ON/io_uring=ON，整合构建路径 /tmp/cpnetflux-runs/DESKTOP-01/build-core。定向24项首轮23pass，token smoke因未设置CPNETFLUX_TEST_TOKEN失败；仅子进程注入随机一次性测试token后重跑通过，不是部署凭据。
最后一次全量CTest：295/295 pass、0fail、0skip，86.01秒；桌面4/4 pass，包含八个C++RPC场景。既有PACKAGING的1项skip是当时io_uring编译OFF，不能当作本轮skip。
最终DESTDIR安装门通过：CLI帮助/API2真实agent/真实create拒绝unsupported/socket0600与退出清理/安装后Qt兼容同步。systemd隔离路径verify exit0，仅宿主机已有snapd RestartMode警告，未安装或启动unit。
strict sanitized hygiene已通过（471文件），无凭据/构建/payload；diff check使用历史CRLF兼容设置。头less截图无法验收，如上明确分报。
证据：build-core.log、targeted-tests.log、targeted-token-retry.log、desktop-final-tests.log、full-ctest-final.log、installed-tests-final.log、unit-verify.log、hygiene-precommit.log，均位于 /tmp/cpnetflux-runs/DESKTOP-01。

## 最终交付候选与 GitHub（2026-10-12）

DESKTOP代码提交/远端回读：2afebf49a4091bf7fc225454453c95ee6079c3e3，codex/DESKTOP-01，push成功一致。此分支包含PACKAGING整合，后续验收记录提交不改变包代码。
PACKAGING分支最终记录SHA：7bfaeac91922fbc17f6e36886d78dbf7c36d1c68；PACKAGING代码0ef9f35e仅代表早期包，最终使用下列2afebf49整合候选。

包目录：/tmp/cpnetflux-runs/DESKTOP-01/artifacts。
- cpnetflux-0.1.0+git.2afebf49a409-x86_64.tar.gz，3057435 bytes；
  SHA256 e3ba70633d2fdc3ef321ff2983355dae94fd85f623411c038711eb1882a9f0de。
- cpnetflux_0.1.0+git.2afebf49a409_amd64.deb，3058218 bytes；
  SHA256 25589bfc761346bf304da3b65286441927492bbeadc9e83b63479e67ecd37880。
- linux-package-manifest.json 与 SHA256SUMS 同目录；manifest固定source_revision=2afebf49a4091bf7fc225454453c95ee6079c3e3、TLS=1、desktop=ON、real_desktop_adapter=false、public_release_ready=false。

TGZ和DEB均解包后安装门通过（含真实agent拒绝create、Qt API2同步）。build-info与manifest一致；包内所有文件无worktree/source路径；DEB无maintainer scripts；ELF依赖可解析；SHA256SUMS全部通过。打包后strict sanitized hygiene再次通过（471文件）。
证据：package-build.log、package-verify.log、hygiene-final.log；隔离解包 package-unpack/，均在DESKTOP-01运行目录。
现场平台Ubuntu22.04.5/x86_64，依赖系统动态库，不宣称跨发行版安装或生产readiness。无LICENSE、无真实adapter、无OS凭据注入、无物理桌面交互验收，不公开发行。
下一步按DESKTOP-SECRETSTORE-01 -> DESKTOP-ADAPTER-01 -> DESKTOP-LIVE-ACCEPT-01推进；当前CLI可用于已认证传输，GUI只完成壳/演示/API安装切片。
回滚源码使用明确git revert；部署按docs/release/linux-install.md保留状态/数据并切回上一包。未改宿主服务/防火墙，未连接上海，深圳根目录HEAD仍eaadeb24490e7e7246766efce94f01a6d41c5b68，既有dirty保留。
