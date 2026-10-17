# GitHub 检查点备份

日期：2026-10-17。

- 比较分支：`codex/desktop-ui-skill-compare-01`。
- 源码/原版预览/方案 A/任务文档提交：`897bcac820864695e8cd291a8606f665f3c8a701`。
- Windows HTTPS push 失败（连接被重置/443 不可达），退出码 128，未将本地 commit 称为已备份。
- 随后导出明确提交的增量 bundle，SHA256：`c13d42538c15c51ee1fa43a06007a51040f256068fbcc3b28331d0b574f33939`。
- 深圳上传文件：`/tmp/cpnetflux-ui-skill-compare-01-897bcac.bundle`，回读 SHA256 一致。
- 独立临时 bare repo：`/tmp/cpnetflux-ui-skill-compare-01-897bcac.git`，通过 shared object 只读引用已有源仓库；没有 checkout、没有操作正式工作树/index，没有修改云端服务或 core。
- 已核实 push remote：`git@github.com:Cantoneseman/CPNetFlux.git`。
- `bundle verify` 通过，fetch 本比较分支后 push 退出码 0。
- GitHub `ls-remote` 回读：`897bcac820864695e8cd291a8606f665f3c8a701 refs/heads/codex/desktop-ui-skill-compare-01`，与源码 commit 一致。
- 本回执作为后续文档提交保存；最终分支 SHA 以该回执提交后的 `git log`/远端回读为准。A 源码与测试对应上面的固定源码 commit。

## 本阶段 B checkpoint

- 用户确认以 Lenny's Newsletter 文章作为 article-guided 来源；候选 B、三方矩阵证据和 B 来源记录提交：`4f2e409d306918f0d623b2910fa4cb78a3ebe275`；比较回执提交：`62ae7c31312be0bf7fc49e3b4d5ad2ba2043494b`。
- 用于备份的完整分支 bundle SHA256：`e770282ea548b4a9423b0c5b29b77d76ac6fd6708d85cc72b0d15f3c66af6c46`；本地 `bundle verify` 通过，上传后深圳 SHA256 回读一致。
- 深圳临时 bare repo 导入 bundle 后，以快进方式推送 GitHub remote `git@github.com:Cantoneseman/CPNetFlux.git` 成功。
- GitHub `ls-remote` 回读：`62ae7c31312be0bf7fc49e3b4d5ad2ba2043494b refs/heads/codex/desktop-ui-skill-compare-01`，与已推送回执提交一致。
- 当前交付包含原版、A 与 article-guided B；不作正式合入、发布或方案选择。之后的纯回执更新以当前任务最终远端回读为准。

## 本轮 B Mission Control checkpoint

- 用户反馈首轮 B 与既有 UI 区别不够，并要求预留更多接口和按钮；本轮将 B 独立重做为 Mission Control 信息架构，提交：`2a0b1066e08d942f2d057ab13756f0df0ed08ff9`。
- 本轮明确保留已有 RPC 边界，新增 B 的创建步骤、任务指挥、节点身份、文件/range、历史和能力诊断页面；未提供 agent API 的入口禁用并写出缺口。预览仍是隔离 synthetic fixture，不是真实 agent 验收。
- 完整分支 bundle：`desktop/ui-comparison/preview-output/mission-control-2a0b106.bundle`；本地 `git bundle verify` 通过，SHA256：`651d7718d6c7a972d3036d73e67c810af2505f95730a19df4759a68be2c11d7f`。
- 深圳上传文件：`/tmp/cpnetflux-ui-skill-compare-01-2a0b106.bundle`，远端 `sha256sum` 回读一致；临时 bare repo 导入和 `bundle verify` 通过。
- 已从深圳临时 bare repo 推送 `git@github.com:Cantoneseman/CPNetFlux.git`；GitHub `ls-remote` 回读：`2a0b1066e08d942f2d057ab13756f0df0ed08ff9 refs/heads/codex/desktop-ui-skill-compare-01`。
- 之后本回执更新需要再提交并推送；截图、`.venv`、bundle 和构建产物均不进 Git。
