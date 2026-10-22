# V3-DESKTOP-UI-REFACTOR-01 结果

日期：2026-10-22
状态：源码实现、真实 loopback 功能与构建门禁已通过；固定版本完整 DEB 已生成并通过解包运行审计；Ubuntu 22.04 GNOME VM 安装验收未完成。

## 输入与执行边界

- 权威目录：深圳 /root/projects/CPNetFlux；当前总指挥单人串行，无子代理、无新 worktree。
- 输入 HEAD：c74eeacd05b327519023badcac657d662b07647b；分支 codex/V3-DESKTOP-NETWORK-BINDING-01；已核实 origin=git@github.com:Cantoneseman/CPNetFlux.git。
- 保留用户未提交 worker-state 文档。提交前发现 HEAD 已由其他写入更新为 8ab530bf0d1fd5de3ccf9070cbc7dd783902a2d1；逐文件核对仅 raw-stage 实验文档与 tools/perf/experiments，无核心/agent/desktop 运行代码变化。本次继承该 HEAD，但不改写或重新纳入这些文件。Git index 经检查为空后才显式暂存本次桌面文件。
- 沿用已确认的浅色桌面视觉，替换正式 QML 客户端；未把 Windows 预览或 demo 当作真实功能。
- 未启动长时间跨域/性能实验，未触碰上海、正式服务或其他项目。临时测试 agent/server 由 finally 回收。

## 实现与接口

沿用 desktop-agent API/schema 3.0.0，JSON-RPC 2.0，runtime=v3；没有新增协议方法或在 QML 维护另一套状态机。

1. agent.getInfo/getContract 握手，核对精确 API、V3 和 adapter 身份；Unix socket 同 UID 校验、错误提示、超时和自动重连。默认 real，demo 必须显式启用且有持续标记。
2. 表单使用合同 policy_defaults/policy_schema/methods；真实 transfer.validate → 用户确认 → create → start。请求带 operation_id，动作带 expected_revision；防重复确认。创建回执丢失后保留原 operation_id，显示核对入口；重放同一请求，不新建另一任务。
3. 初次连接和重连都 reset subscribe，恢复持久任务；过期快照明确标记。全部任务事件更新列表，不因选中别的 attempt 漏任务。
4. 真实 queued 任务可手动 start；cancel 显示请求/终态，resume 只在 agent 返回 eligible/checkpoint 时开放。UI 退出不杀 agent。
5. 详情展示真实阶段、大小、checksum/manifest/finalize、错误、请求与实际策略。未知字段显示未知；attempt/file/range 各自维护分页 watermark，range 使用真实 file_id/generation。
6. 保留并整合节点包导入、CA/token 文件引用、TLS 认证检查、本机地址/逐 lane peer mapping。原有 C++ 私有导入路径保留；QML 不接收 token 或私钥内容。
7. 补齐 agent 的最小观测缺口：取消可能留下 manifest 而无 summary；读取非 symlink manifest 前四个头字段，核对版本、方向、source logical path 和 checksum 后仅声明恢复候选。源身份、已完成/缺失 range 仍由 V3 core resume 核验。data_connect 直接回读核心 summary，不推算。
8. 安装 wrapper 默认发现同安装目录的核心，保留 CLI/env 覆盖；user unit 安装至 lib/systemd/user，不再指向旧用户核心目录，不使用 PrivateTmp 隔离用户选择的路径。
9. DEB 显式依赖 qt6-qpa-plugins、qml6-module-qtquick-templates、dialogs 及其余使用的 QML 模块；测试探针不安装。

## 验证命令与结果

集成 Release 构建目录：/tmp/cpnetflux-desktop-integrated-build，TLS=ON，CPNETFLUX_BUILD_DESKTOP=ON，io_uring=OFF。

```bash
cmake -S . -B /tmp/cpnetflux-desktop-integrated-build -G Ninja \
  -DCPNETFLUX_BUILD_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cpnetflux-desktop-integrated-build --parallel 2
CPNETFLUX_TEST_TOKEN=<temporary-test-token> ctest \
  --test-dir /tmp/cpnetflux-desktop-integrated-build --output-on-failure
ctest --test-dir /tmp/cpnetflux-desktop-integrated-build \
  -R '^desktop-|^cpnetflux_service_config$' --output-on-failure
python3 -B desktop/tests/test_ui_real.py \
  --ui-build /tmp/cpnetflux-desktop-integrated-build/desktop \
  --core-build /tmp/cpnetflux-desktop-integrated-build \
  --evidence-dir /tmp/cpnetflux-desktop-refactor-delivery-final-v2
```

- 完整 CTest：422 项无失败，421 passed、1 io_uring smoke skipped（构建未启用该能力），197.51 秒。日志 /tmp/cpnetflux-desktop-integrated-final-ctest.log。
- queued start 补充后的定向 CTest：7/7，40.07 秒。日志 /tmp/cpnetflux-desktop-final-targeted-ctest.log。finalize 对象显示修正后再跑定向门禁 7/7、40.34 秒，日志 /tmp/cpnetflux-desktop-display-ctest.log。
- desktop-v3-api 共 19 测试；service 配置测试 8 项。C++ RPC 专门核验初次 reset、合同不匹配、epoch 重连、创建回执丢失后相同 operation_id 重放。
- 正式生产 QML 资源经 C++ UI 流程探针驱动真实按钮，所有 transfer adapter=real、runtime=v3。测试在 127.0.0.1 启动 TLS/token V3 server，不使用合成传输。
- 69 文件目录（40 MiB 随机文件、多个小文件、嵌套与空文件）上传和下载，SHA-256 树哈希相等；manifest observed、CRC32C pass、finalize pass、commit_confirmed=true；文件分页、generation=69 的 3 个 range 通过。
- 仅含一个文件的目录经统一 tree API 传输成功；不将其标为独立单文件 API 已实现。
- 128 MiB 任务：进行中退出 UI，重开可从同一 agent 恢复任务，后端继续并完成；另一任务等待真实已验证 chunk checkpoint 后取消，再以新 attempt resume，summary.resume=true 且目标哈希一致。
- 已持久化 queued 任务重新打开详情，真实调用 transfer.start 后完成。
- 错 token 与错误 TLS identity 真实认证失败；越界远端路径在 validate 阶段拒绝；独立单文件返回 unsupported。
- 760/900/1180 宽度与 100%/125%/150% 字体缩放页面截图已检查；不把离屏截图当作 GNOME VM。
- 最终 loopback 证据：/tmp/cpnetflux-desktop-refactor-delivery-final/result.json 及各 *-result.json、*.stderr、page-*.png。证据含测试路径，原始凭据/payload/state 仅留隔离目录，不提交或打包。

## 失败与修正记录

- 首次完整 CTest 两项认证测试缺 CPNETFLUX_TEST_TOKEN；补临时测试凭据后通过，未将凭据写入源码。
- 调查过一次 Release RPC 探针失败：编译期间源文件更新使对象时间戳掩盖旧对象，明确触发 rpc_client.cpp/.h 重编后消失；随后完整 422 门禁无失败。
- 最后 finalize 显示微调的临时补丁发生内容截断，离屏门禁立即捕获 QML 语法错误；恢复完整内容并核对 SHA-256 后，桌面与 service 7/7 再次通过。
- 测试双 lane 必须给隔离 server 显式 --link-set-max-links 2；默认单 lane 拒绝能力匹配是预期结果。
- 已确认核心残余问题：global/adaptive scheduler，128 MiB 文件取消后 resume，默认 64 MiB work-item 预算组合出现 scheduler worker admission timed out。证据 /tmp/cpnetflux-desktop-refactor-range-final-02。默认 scheduler_mode=off 的恢复通过；高级恢复改为显式 min=max=256 MiB 预算后通过。本轮不改核心，不宣称所有高级策略组合已通过。

## 未完成能力与安装边界

- 后端未支持：统一认证独立单文件、pause、远端目录浏览、客户端 mTLS 配置、管理员/Secret Service 凭据、实时逐字节/块速率、恢复前缺失 range 预览。UI 禁用或标明原因，不伪造成功。
- node.testConnection 可验证 TLS/token；目录写权限/数据口未验证，ready=false 不改写为全绿。
- 默认策略要求 control CA/identity、data TLS、CRC32C、reject fallback；没有匿名/off-TLS 默认路径。
- Ubuntu 22.04 GNOME 干净 VM 安装与图形会话验收待提供可用 VM。发现 WSL 发行版不能视为干净 GNOME VM。未修改深圳已运行服务。
- 包审计将解包检查运行依赖、无凭据/状态/payload/测试探针，使用包内 wrapper 启动 real agent 与 offscreen UI；此结果只证明解包运行。

## 提交与安装包

- 实现提交：454c9e3ad96aabac0a3d1fdf71c0b74de7fd9c22，已推送 origin/codex/V3-DESKTOP-NETWORK-BINDING-01 并用 git ls-remote 回读一致。
- 构建机为 Ubuntu 22.04.5 LTS；提交后重新 CMake configure/build，再 CPack，包内 source_revision 精确指向实现提交。
- 完整安装包：/tmp/cpnetflux-desktop-integrated-build/package-delivery/cpnetflux_0.1.0+git.454c9e3ad96a_amd64.deb（约 4.3 MiB）。
- SHA-256：489ba1f253e616665cd59158f9ec2f5dc654ab25a183738aae3020f676b3aa81。
- CPack 日志：/tmp/cpnetflux-desktop-delivery-cpack.log；构建日志：/tmp/cpnetflux-desktop-delivery-build.log。
- test_package.py 审计通过：27 个安装文件，wrapper 自动发现包内 sibling core，adapter=real/API=3.0.0；解包 UI offscreen 启动、连接并退出后 agent 仍存活；未包含测试探针、凭据、payload 或状态。
- Depends 已核对包含 qt6-qpa-plugins、qml6-module-qtquick-templates、qml6-module-qtquick-dialogs 等；包内 build-info 的 public_release_ready=false 保留。
- Windows 仅接收包和截图备份，路径 C:/Users/12563/.codex/visualizations/2026/10/12/01a12fd5-7f76-72a0-886a-274b0159ee7c；本机 Get-FileHash 回读 SHA-256 一致。没有在 Windows 开发源码。
- 未 apt 安装到深圳主机，未切换已运行的 user/system service。解包运行不等于干净 GNOME VM 安装已通过。

```bash
cpack --config /tmp/cpnetflux-desktop-integrated-build/CPackConfig.cmake \
  -G DEB -B /tmp/cpnetflux-desktop-integrated-build/package-delivery
python3 -B desktop/tests/test_package.py \
  /tmp/cpnetflux-desktop-integrated-build/package-delivery/cpnetflux_0.1.0+git.454c9e3ad96a_amd64.deb
sha256sum /tmp/cpnetflux-desktop-integrated-build/package-delivery/cpnetflux_0.1.0+git.454c9e3ad96a_amd64.deb
git ls-remote origin refs/heads/codex/V3-DESKTOP-NETWORK-BINDING-01
```

## 回滚与下一步

保留现有 dirty 文件后，仅撤销本阶段明确代码提交；输入 c74eeacd05b327519023badcac657d662b07647b 可作为恢复指定桌面文件的基线。不得 reset/clean 或删除实验/其他项目数据。正式服务本轮未切换，无服务回滚动作。

下一步先在用户指定 Ubuntu 22.04 GNOME VM 安装同一 SHA-256 的 DEB，按 user service 启动 agent/UI，确认 adapter=real 并跑最小认证传输。独立单文件/mTLS/实时 range 预览如要开放，先补后端能力合同，再启用 UI。
