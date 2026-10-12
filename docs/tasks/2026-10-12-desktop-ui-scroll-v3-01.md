# DESKTOP-UI-SCROLL-V3-01 范围与验收

输入提交：`eaadeb24490e7e7246766efce94f01a6d41c5b68`。已核实当前深圳根树 `desktop/` 干净；桌面 UI 修订 `323f94d64a44f6b922d17c4796daecd6f1e7eb0c` 仅存在于独立分支，未进入本分支，故按原提交集成后继续修滚动。

## 范围
- 集成上轮已审查的 CPNetFlux 桌面界面文案、导航选中态和响应式布局。
- 修复详情/表单/节点等高内容页面在内容溢出时未形成可滚动高度的问题。
- 增加滚动视口回归断言，覆盖 1180×780、900×650、760×560 窗口。
- 查明并记录 V3 range striping 在当前桌面连接中的不可用原因；界面明确显示当前状态。

## 非目标
- 不实现真实 agent transfer adapter，不把桌面任务接入核心传输引擎。
- 不把独立 V3 分支合入当前核心开发树，不运行跨主机/性能实验，不宣称 V3 生产或 WAN 验证完成。
- 不改 `desktop/agent` RPC/schema、核心协议/数据面、Linux 安装器或已有安装工作树。

## 验收标准
- 当前桌面源包含上轮已推送的 UI 改动；旧版文案和导航状态不再是深圳根树默认版本。
- 详情页 contentHeight 大于 viewport 时存在可滚动距离；离屏测试覆盖窗口尺寸且无 QML 错误。
- Qt CMake build 与完整 CTest 通过。
- 文档根据当前源码/分支证据解释 range striping 的具体阻塞边界。

## 本阶段文件范围
- `desktop/ui/Main.qml`
- `desktop/ui/main.cpp`
- `desktop/tests/test_ui.py`
- `docs/tasks/2026-10-12-desktop-ui-scroll-v3-01.md`
- `docs/coordination/BOARD.md`
EOF


## 执行与验收结果（2026-10-12）
- 已将上一轮桌面 UI 修订集成到当前 Shenzhen 分支：提交 `1c3e39807078e1e52d86dc91e84d596a44fd3a91`。本轮仅修改任务详情滚动、表单横向自适应和回归检查。
- 根因由 Qt offscreen 运行态诊断确认：任务详情滚动区 viewport 为 570 px，而固定卡片 contentHeight 只有 480 px；卡片内部内容溢出但滚动内容没有超过视口，因此滚轮没有可移动距离。现将详情卡片设为可滚动内容高度，并令详情/节点配置/历史的 ColumnLayout 宽度跟随 ScrollView.availableWidth。
- `desktop-offscreen` 在 1180×780、900×650、760×560 三种窗口尺寸遍历七个页面；详情页通过 `--layout-report` 断言 contentHeight > viewport，并继续检查 QML 错误。验证命令：`cmake --build /tmp/cpnetflux-ui-scroll-v3-01`（成功，ninja 无待构建项）；`ctest --test-dir /tmp/cpnetflux-ui-scroll-v3-01 --output-on-failure`（3/3 通过，23.60 秒）；`git diff --check`（通过）。
- 旧的 range 页面状态容易被误读为已支持。源码显示 UI 的 `loadNextRanges()` 只调用 `range.list` 分页读取状态；它不启动分片传输。桌面当前启动的 Python agent 是 demo adapter：非 demo 的 `transfer.create` 明确返回“真实传输 adapter 尚未接入”；demo 的 `node.capabilities` 返回空 schedulers/range_policies、最大通道数 1；模拟结果固定为 `actual_mode=v1`、`actual_scheduler=static`、`actual_range_policy=file`。因此 V3 range stripping 目前没有接到桌面可执行路径，页面已明确提示参数选择不会启动 range 并行传输。
- 核心 V3 候选仍在单独分支 `codex/V3-UNIFIED-01`，当前提交 `e88d46c3d43f08190193fa4907815834f84028c6`。已读到的结果记录只证明定向测试和 loopback range 上传/下载正确性，不构成跨域性能验收；本任务未合并该核心分支、未运行跨主机实验。
- VM 上的旧进程/安装包不会随着源码提交自动更新。本次完成的是源代码修复和深圳构建/测试，尚未在用户 VMware 中安装或启动新版本。后续应先实现真实 agent adapter 与 capability/策略协商，再构建并更新 VM 包，最后在 VM 实际滚轮验证及对 V3 range 传输做端到端验证。
