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

当前交付仅原版与 A；B 因缺少用户指定 skill 来源尚未制作。此 checkpoint 不表示两个候选已齐备，不作正式合入或发布。
