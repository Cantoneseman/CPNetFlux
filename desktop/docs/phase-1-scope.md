# DESKTOP-DESIGN-01 第一阶段范围

日期：2026-10-10；输入 HEAD：de8e9efb274dcc05a6460225376a61c1350e8b9d。
分支：codex/DIR-V2-TRANSFER-ENGINE-01；权威路径：深圳 /root/projects/CPNetFlux。
用户本轮已串行交接桌面设计写入权；既有核心 dirty 文件保持原样。

目标：为普通 Linux 超算学生/老师定义本机目录 ↔ 受控远端 CPNetFlux 节点的页面、认证、生命周期及可验证 JSON-RPC 草案。
非目标：真实传输接入、V2/V3 核心实现、云端实验、账号/配额系统、远端↔远端、修改根 CMake 或核心目录、安装依赖。
只写 desktop/。不创建聊天或并行写入者。V1 作为兼容 fallback 保留；V2 是引擎，V3 是动态文件/range 调度，不把二者混成一个开关。

受影响文件（均新增）：
- desktop/docs/phase-1-scope.md
- desktop/docs/product-and-state.md
- desktop/docs/rpc-contract.md
- desktop/docs/acceptance.md
- desktop/docs/phase-1-receipt.md
- desktop/schemas/desktop-agent.schema.json
- desktop/ui/wireframes.md
- desktop/agent/adapter-boundary.md
- desktop/tests/test_contract.py
- desktop/tests/fixtures.json

验收：草案覆盖全部用户指定方法；请求/响应/事件/错误可用 Draft-07 validator 校验；负例拒绝未知方法、明文凭据、缺失回退原因和不完整 completed；场景向量包括重连/取消/恢复/认证/权限；文档明确仅契约测试通过，不当作后端功能验收。
命令：python3 -B desktop/tests/test_contract.py；git diff --cached --check -- desktop；精确提交上述文件、push origin 当前分支、git ls-remote 回读。
先完成设计/schema，再决定 shell；Qt/libsecret 开发包当前不存在，首阶段选择线框及构建设计，不安装、不宣称可运行 GUI。
