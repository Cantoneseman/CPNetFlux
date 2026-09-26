# CLOUD-GITHUB-01-RECONCILE-QA 独立核验

- 路线：`R2026-09-24.16`
- 结论：**BLOCKED（迁移门禁未通过）**。548 条迁移文件的清单结构和源/目的内容一致性通过；深圳目的树仍保存旧版 551 条控制清单及其旧摘要，故当前目的端状态没有与修订清单对齐。
- 审查输入：任务提交 `6779c87a3bb8d9756ef4390fda758f88638b72ef`。任务 worktree `/tmp/cpnetflux-runs/CLOUD-GITHUB-01/reconcile/src` 与本 QA worktree `/tmp/cpnetflux-runs/CLOUD-GITHUB-01/reconcile/qa` 均从该提交开始；审查前两者 HEAD 均为该 SHA、工作树干净、index 空。本 QA 分支为 `codex/CLOUD-GITHUB-01-RECONCILE-QA`。
- 源：只读 `D:\Project\CPNetFlux`，清单记录的 source HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。目的项目树只读 `/root/projects/CPNetFlux`，HEAD `2523956be60217df377306f3493fa749397662b4`；该树预先已有 273 个未暂存状态项、index 为空，本审查未更改它。

## 通过的清单与逐文件核验

审查输入中的 `docs/coordination/CLOUD-GITHUB-01-FILES.json` 为 98,753 bytes，SHA-256 `f7f962862a7676147f36518600c21ba7edf0d39aa2b4365d4b5680d3f2bc699d`。JSON schema 为 `cpnetflux-cloud-github-01.files.v1`，`source_head` 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，含 548 个 `entries`。逐项字段恰为 `path`、`size`、`sha256`；路径是已排序、唯一、相对 POSIX 路径，没有绝对路径、反斜杠或 `..` 段；尺寸是非负整数，摘要为 64 位小写十六进制 SHA-256。

同一输入中的 `CLOUD-GITHUB-01-MANIFEST.json` 为 1,272 bytes，SHA-256 `46024cc4bbc130592fde0fe8a66797134913e8ffe1624d2bf27709158e530786`。其 per-file manifest 摘要和数量为上述 `f7f...699d`、548，与原始 JSON 字节计算一致。

我从 Windows 只读源树独立递归读取文件内容并计算尺寸和 SHA-256：迁移范围 548 文件，扫描错误 0，清单缺项 0、额外项 0、尺寸/哈希差异 0。随后从深圳只读目的树重新计算同一批 548 个路径：每条都存在，尺寸/哈希差异 0。目的树按实际 5 条路径/目录排除规则扫描为 552 个普通文件，因此这 548 项之外另有 4 项控制/验证产物，详见下表。清单数据层面的 `548/548` 内容 parity 为 PASS。

## 目的端额外文件与阻塞

| 目的端额外路径 | 实际证据 | 解释与影响 |
| --- | --- | --- |
| `.cpnetflux-migration-marker` | 36 bytes；SHA-256 `5dc678086ed8d00feefae15aee18efcbc9a3d7ec98e5ff74fbdd6ca1d3babe1d`；字节内容中 `created=` 后为空，随后是反斜杠和换行符 | 迁移所有权标记，不是源迁移文件。时间字段未填写，不能由此确认创建时间；请 00 修正或将时间记为未知。未修改。 |
| `docs/coordination/CLOUD-GITHUB-01-MANIFEST.json` | 目的端 937 bytes，SHA-256 `6521b4ba5443fdcfa3d870f114ba0add08e99ef02a998d849011584b19e24806` | 迁移摘要控制文件，但指向目的端旧 per-file 清单，且没有修订摘要中的 `verification` 对象。审查输入修订版为 1,272 bytes、SHA `46024c...` 并记录 548/548、零差异。目的端需同步为明确的权威版本。 |
| `docs/tasks/2026-09-25-cross-domain-smoke-result.md` | 2,551 bytes；SHA-256 `58cacf0470035b8b53c47900457a2fe37289577b14e6bf89751f7fcd35b84671` | 云端验证记录，不是从 Windows 迁移来的源文件。记录 smoke 证据和目录下载将内部 manifest 作为普通文件处理的观察；不能并入 548 项源文件 parity。 |
| `docs/coordination/CLOUD-GITHUB-01-FILES.json` | 目的端 99,218 bytes，SHA-256 `1183b6327b7467883b897ba1d934de6f1f660b28a4079e4f692d587dd732dbd4`，包含 551 项 | 额外的生成审计清单。其 548 个共同路径/尺寸/哈希与审查版完全相同，另加上表中 marker、摘要和 smoke 记录 3 项；但它不是待验收的 548 项修订版。目的端旧摘要准确指向此旧文件，说明两者内部匹配但整体未更新。该第四项生成控制文件未单独列在 summary 的三项 `known_remote_only_artifacts` 中。 |

**阻塞**：目的端的两份清单文件不是审查输入中具有 548 项和零差异核验信息的版本。虽然被列出的 548 个迁移文件全数一致，这仍不能证明目的端保存了本次要验收的完整逐文件 manifest 和 summary。另有 schema 口径问题：修订 per-file manifest 的 `exclusions` 有 6 项；最后一项是自然语言 `cloud-only migration marker and validation artifacts`，不是路径或 glob。summary 只列 5 条实际排除规则。我的扫描按前 5 条规则执行并单独列出实际额外产物；建议将这句从机械 exclusions 移出，在独立 artifact 列表按精确路径列出 4 个目的端产物。

## Git 状态、推送与范围

QA worktree 的 `origin` 为 `git@github.com:neoClav/Computing-Power-Network-Transmission.git`。只读 `git ls-remote --symref origin HEAD refs/heads/main refs/heads/master refs/heads/codex/CLOUD-GITHUB-01-RECONCILE` 返回退出码 0；远端 `main`/`HEAD` 为 `16b377359494f19f386ba5d375d353449e45f7a0`，且现有 `codex/CLOUD-GITHUB-01-RECONCILE` 指向任务提交 `6779c87a3bb8d9756ef4390fda758f88638b72ef`。origin 仓库路径仍采用旧项目名，但它已有本任务准确的 GitHub 任务分支；本 QA 结果已按任务要求只提交该结果文件并推送自己的分支；最终回读的远端 `refs/heads/codex/CLOUD-GITHUB-01-RECONCILE-QA` SHA 与本地 HEAD 一致（确切提交 SHA 见交接）。此 QA 分支备份不表示迁移门禁通过。

本轮只新增本 QA worktree 中的 `docs/tasks/2026-09-25-cloud-github-01-qa-result.md`。未更改 Windows 项目树、任务 worktree、`/root/projects/CPNetFlux`、BOARD/ROSTER 或其他路径；未运行构建、测试、实验、删除或远端项目树写入。深圳项目树原有的 273 项 dirty 状态未被本 QA 工作触碰。

## 实际核验及门禁

- PowerShell 调用 Windows Python 只读取深圳 QA 清单并扫描 `D:\Project\CPNetFlux`：退出码 0；548 项，0 扫描错误、0 缺失、0 额外、0 尺寸/SHA 差异。
- 只读 SSH 到 `gridflux-beta-shenzhen`，用 Python 解析输入 JSON、验证摘要/结构/路径，并重新计算 `/root/projects/CPNetFlux` 文件 SHA：退出码 0；548 项逐文件匹配，目的范围 552 个文件、4 个额外产物。
- 只读 Git `rev-parse`、status 与 staged-path 检查：退出码 0；QA 起始 worktree 干净、任务 worktree 干净；目的树 HEAD 为 `2523956...`、273 个既有 dirty 项、staged 0。
- 只读 `git ls-remote`：退出码 0；远端引用如上。构建、测试和实验：未运行（不在任务范围）。
- 初次探查曾误用 JSON 的 `files` 键；发现 schema 实际为 `entries` 后，该次空计数结果作废，后续所有结论来自按 `entries` 重跑且退出码 0 的检查。
- 一次通过 PowerShell 管道送入 Bash 的状态计数命令因 CRLF 使 wc 收到带回车的参数而失败（非零退出）；随后改用直接 porcelain/status 查询与 Python 计数，确认 QA worktree、任务 worktree 干净，目的树仍为 273 项 dirty、index 空。该脚本错误没有改变目的树状态。
- 写入后门禁：UTF-8 严格解码、无 BOM、LF-only、末尾换行、无尾随空白均 PASS（结果文件最终字节数在提交前复核）；`git diff --cached --check` 和提交对象 `git show --check` 均退出码 0。提交只含本结果文件；`git push origin codex/CLOUD-GITHUB-01-RECONCILE-QA` 退出码 0，最终 `git ls-remote` SHA 与本地 HEAD 一致。最终回读确认任务输入哈希未变，任务 worktree 干净，目的树 HEAD、273 项 dirty 状态指纹和空 index 未变；QA worktree 干净、index 空。

## 下一步

00 应先将修订版 per-file JSON 与 summary 同步到深圳目的树的权威迁移位置，明确将 per-file manifest 本身作为生成控制文件而非迁移条目，改为机器可执行的排除规则并逐条解释四个目的端额外文件；再处理 marker 空时间字段（修正或明确标记未知）。随后重新读取并核实目的端清单摘要、548 项数量、源/目的尺寸与 SHA 零差异，以及修订后迁移清单所在提交的 GitHub 远端 SHA，之后再决定是否清除迁移 gate。当前结论不授权 00 宣布迁移验收通过。
