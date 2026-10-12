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
