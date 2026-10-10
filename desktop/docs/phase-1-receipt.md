# DESKTOP-DESIGN-01 回执

日期：2026-10-10。输入 HEAD：de8e9efb274dcc05a6460225376a61c1350e8b9d；分支 codex/DIR-V2-TRANSFER-ENGINE-01。
只写 desktop/，未改核心目录、根 CMake、任务板、保护目录；未启动实验、安装软件或创建聊天。

产物共 10 文件：docs/phase-1-scope.md、docs/product-and-state.md、docs/rpc-contract.md、docs/acceptance.md、docs/phase-1-receipt.md、schemas/desktop-agent.schema.json、ui/wireframes.md、agent/adapter-boundary.md、tests/test_contract.py、tests/fixtures.json（均相对 desktop/）。
交付页面流程/线框、任务文件range attempt状态、五步认证与数据口测试、15个方法、类型化事件与错误、幂等和重连快照规则。schema 是 draft-1 原型，不是底层正式能力承诺。

验收：python3 -B desktop/tests/test_contract.py：6测试通过，47正例/16负例覆盖。git diff --cached --check -- desktop：PASS；暂存集合精确等于上述10文件。
初次门禁失败原因：系统 jsonschema 3.2.0 无 Draft202012Validator；改为其支持的 Draft-07，保持条件/引用约束。未安装依赖。
全仓 git diff --check 另因既有核心/docs dirty CRLF/trailing whitespace 失败，不修改无关文件；改用本阶段 desktop 精确 staged 门禁。

未实现：Qt/QML可执行shell、agent、真实Unix socket服务、libsecret接入、节点测试和真实transfer执行/取消/恢复。契约向量不是后端集成测试。现有 Qt/libsecret 开发包缺失，选择线框/构建方案交付。
下一步：00确认按mode能力矩阵、实际安全策略回读、结构化事件版本、取消收口与checkpoint恢复、安全凭据注入及目录授权；补实体分页快照，冻结draft-2后再串行实现shell和真实接入。
GitHub设计提交：3e948784f1f2a45fccfd1a66a00a4fa6f3b3e048。git push origin HEAD:refs/heads/codex/DIR-V2-TRANSFER-ENGINE-01 退出0；git ls-remote origin refs/heads/codex/DIR-V2-TRANSFER-ENGINE-01 回读 3e948784f1f2a45fccfd1a66a00a4fa6f3b3e048，一致。desktop 工作区干净，index 为空。
本次回执补录形成后续单文件提交；其最终SHA/远端回读在本轮最终回复中返回。
