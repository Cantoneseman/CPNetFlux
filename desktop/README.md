# CPNetFlux Linux Desktop

Qt 6/QML 客户端连接当前用户的 Unix socket agent，使用 desktop-agent API 3.0.0 与 V3-only 核心。默认是真实模式；节点、任务、取消、恢复和核验均由 agent 提供。视觉沿用已确认的浅色桌面设计。Windows 副本不是源码权威。

## 构建与真实运行

构建依赖：Qt 6.2+ Core、Gui、Qml、Quick、QuickControls2、QuickDialogs2、Network，CMake 3.20+、Ninja、C++20、Python 3 与 jsonschema。
Ubuntu 22.04 运行依赖还包括 qt6-qpa-plugins、qml6-module-qtquick-templates、controls、dialogs、layouts、window 与 workerscript。独立 desktop 包不包含核心客户端；完整安装包使用根 CMake 的 CPNETFLUX_BUILD_DESKTOP=ON。

```bash
cmake -S desktop -B /tmp/cpnetflux-desktop-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cpnetflux-desktop-build --parallel 2
ctest --test-dir /tmp/cpnetflux-desktop-build --output-on-failure

# 使用已构建的、与当前 V3 兼容的核心程序目录。
python3 desktop/agent/cpnetflux_agent.py --core-bin-dir /path/to/core-build \
  --runtime-dir "$XDG_RUNTIME_DIR/cpnetflux" \
  --state-dir "$HOME/.local/state/cpnetflux"
/tmp/cpnetflux-desktop-build/cpnetflux-desktop --socket "$XDG_RUNTIME_DIR/cpnetflux/agent.sock"
```

runtime/state 目录必须归当前用户且权限为 0700。桌面默认连接 $XDG_RUNTIME_DIR/cpnetflux/agent.sock。UI 退出不会终止 agent 或传输。首次连接与重连会请求 reset subscription，恢复持久任务；离线期间保留快照并标记过期。

## 页面流程

- 连接设置：创建/修改节点，选择 CA 与 token 文件引用，或导入节点包。token 只由 C++ 导入器写入当前用户私有目录；不进入 QML、RPC 或 profile。认证检查不探测目录权限和数据口，不将 authenticated 当作 ready=true。
- 新建传输：选择上传/下载目录、本地绝对目录、节点和 root-relative 远端路径。表单从 policy_defaults/schema 初始化；transfer.validate 展示解析值与 warnings，确认后 create/start。修改表单会清除预检结果；重复确认不会重复创建。
- 任务与详情：订阅真实状态；取消请求与已静止终态分开。eligible checkpoint 才允许调用 transfer.resume；新 attempt 的身份仍由核心核验。最终 summary、manifest、CRC32C、finalize、请求/实际策略分别展示；未提供的速率、计时和缺失 range 预览显示未知。
- 文件/range/执行记录：每个查询保留自己的分页 snapshot 与 watermark；按实际 file_id/generation 查询 range，空文件或整文件任务可以没有 range。coverage=observed_only 不代表完整实时目录。
- 网络绑定：get/inventory → plan（自动或手选本机地址、逐 lane 远端 IP）→ 确认 apply → verify/clear。所有远端地址必须属于同一 V3 服务/root；不改变系统 IP 或路由，不声称物理路径已验证。
- 日志与诊断：socket、API/runtime、agent 状态、方法支持/原因、稳定错误码和当前能力边界。

当前不支持：认证独立单文件任务、暂停、远端目录浏览、管理员/密钥环凭据、客户端 mTLS 配置、实时逐块速率、恢复前缺失 range 清单。相关入口禁用或明确说明原因。仅含一个文件的目录仍通过统一 V3 tree API 传输。

高级策略边界：global/adaptive scheduler 在取消后 resume、128 MiB 文件与默认 64 MiB work-item 预算组合下，已观察到 worker admission timeout。本轮不修改核心 scheduler；默认 scheduler_mode=off 的恢复已通过，高级恢复门禁使用显式 256 MiB work-item 预算。不要据此承诺所有高级组合均可恢复。

## 功能验收

```bash
python3 -B desktop/tests/test_ui_real.py \
  --ui-build /tmp/cpnetflux-desktop-build \
  --core-build /path/to/core-build \
  --evidence-dir /tmp/cpnetflux-desktop-real-unique-run
```

此门禁在 127.0.0.1 启动隔离的真实 TLS/token V3 服务和 agent，通过实际 QML 按钮预检、确认、取消和恢复；包含上传、下载、文件分页、UI 进行中退出/重开、认证错误、路径拒绝及哈希核验。凭据、payload、截图和状态仅在独立 evidence 目录；不进入源码或安装包。不会发起跨域/性能实验。

离屏截图不是 Ubuntu GNOME 虚拟机安装验收，后者仍需独立图形会话检查。

## 显式开发诊断

只有显式启动 agent --demo 才使用合成数据，界面持续显示“开发诊断 / 合成数据”。默认不自动回退 demo；demo 与 real 切换时清除旧 adapter 的任务快照，重新同步。
