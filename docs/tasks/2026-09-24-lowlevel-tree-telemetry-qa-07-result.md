# LOWLEVEL-TREE-TELEMETRY-QA-07 结果：PLAN-07 两项更正复审

日期：2026-09-24（Asia/Shanghai）
路线/任务：`R2026-09-24.9 / v1`
结论：**PASS FOR IMPLEMENTATION DESIGN GATE（仅静态规格与 fixture gate）**。建议 00 在独立 worktree 创建窄 telemetry implementation task；本结论不代表 parser、传输、性能或产品验收通过。

## 范围与输入稳定性

本轮只复审 PLAN-07 的两个更正，并检查 PLAN-06、QA-06、ARBITRATION-03、ARCH-REVIEW-01、tree telemetry decision 和固定 `file_transfer_client.cpp` 未被隐式改写。资料 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；固定源码基线为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。

读前/读后 SHA-256、修改时间和大小稳定，PLAN-07 派单哈希精确匹配：

| 输入 | SHA-256 |
|---|---|
| PLAN-07 result | `2DEDDF4FECB60C221CB47428014F9550386FCF7DFF848E2B7EB282DFA3BF8EEF` |
| PLAN-06 result | `8FE5440B33B777FB395C99101C9BD804FC036AF6B80435F0EC12D4DE65083BC2` |
| QA-06 result | `E1619B1BADE79AD63A63F4600E89CD81CA91677B05F7E4E10AB10E225BB4DBC7` |
| ARBITRATION-03 result | `75A3FE5DFBF82D2D3237FA0205F23B99470235D361A82FC84CCD4C2313BCC9FC` |
| ARCH-REVIEW-01 result | `ED9D7B0FDFA4DA29003F224377B654A7A9CAF39146CBF7CE47BD618CDD7780F0` |
| tree telemetry decision | `C55991CD0781B24B24E6B05C295329B41B5DFF23F6149868366FF8A8596F928C` |
| `src/core/io/file_transfer_client.cpp` | `AD9E03B51368BD66284059AF7291962E8314590EA6B690042044A96AE1982939` |

固定源码 `file_transfer_client.cpp:390-413` 的 `sendStream(..., std::uint32_t streamId, ...)` 将该参数传入 SessionInit，支持 R-SAME 使用真实协议 `stream_id=0`。三份固定源码相对 `3b0820d..HEAD` 的差异检查退出码为 0。

## 两项更正

1. **`span_id` 下界：PASS。** PLAN-07 §1.1 明确仅接受 `1..UINT64_MAX`，保留其它允许为零的 uint 字段规则，并新增完整 26 键 raw JSON 向量 `span_id=0`，期望错误为 `span_id_out_of_range`；同时写明 1 和 `UINT64_MAX` 边界接受、上溢拒绝。该向量不计入六组 golden 的 44 行，边界与 ARCH-REVIEW-01 一致。
2. **R-SAME upload stream ID：PASS。** PLAN-07 §2 的 R-SAME-CONTROL 明确四行 upload `stream/data_connect`（attempt 1/2 各 start/terminal）均为真实 `stream_id=0`、`control_id=5`；attempt 1/2 使用不同 span（121/123）并保持 attempt 区分。与固定源码参数和 QA-06 原阻塞一致。
3. **PLAN-06 其余输入保持：PASS（静态核对）。** PLAN-07 明确为 PLAN-06 衍生副本，只列出上述两项规范修正；六个 golden block、schema 矩阵、logger 注入、性能门、实现白名单和测试顺序未见其它输入 hash 或架构字段变化。静态核对不替代逐字差异工具或实现测试。

## 独立 fixture lint

使用 PowerShell 只读脚本从 PLAN-07 六个 `jsonl` fence 提取并逐行 `ConvertFrom-Json`，另以原始文本 token 检查重复键和固定键序；检查 lifecycle、时间差、paired N/A、interfile idle 的 next ID、attempt-0 形状及 R-SAME 语义。结果：

`PLAN07_STATIC_LINT=PASS blocks=6 rows=44 block_rows=16,6,3,8,8,3 keys=26/18 duplicate_keys=0 span=1..UINT64_MAX R-SAME(stream_id=0,control_id=5,attempt/span-separated)=OK span_id_zero(full-26-key-vector)=reject lifecycle/N/A/idle/retry/attempt0=OK`

该 lint 退出码为 0。它是文档 fixture 的纯函数/静态检查，不是项目 validator 的运行态拒绝证明。

## 门禁与未运行项

- `git rev-parse HEAD`：退出码 0，HEAD=`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，未变。
- `git diff --cached --name-only`：退出码 0，无输出，暂存区为空。
- 固定三源码 `git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`：退出码 0。
- 输入 SHA-256/mtime 读后复核：与读前相同。
- 结果文件按严格 UTF-8、无 BOM、纯 LF、无行尾空白写入；`git diff --check` 退出码 0。其提示仅涉及共享工作区既有文档的 LF/CRLF 转换，不是本结果文件问题。

以下全部 **NOT_RUN**：运行态 raw-token parser/validator、logger 四类故障注入、真实 upload/download、hash/resume 等价性、telemetry on/off、overhead 门、CMake/CTest、SSH、云端实验和清理。fixture lint 不能升级这些状态，也不能宣称 performance eligible 或产品正确性。

## 结论与下一步

两个 QA-06 规格阻塞已由 PLAN-07 精确闭合，未发现 PLAN-06 其余约束或固定架构输入被隐式改写。因此允许 00 创建**白名单、独立 worktree、窄范围** telemetry implementation task；实现任务必须按 PLAN-07 的 parser/golden、lifecycle/ID、logger isolation、mixed consumer、真实 loopback、on/off 等价性和 overhead 顺序逐项验收。任何动态门禁失败都应保持实现/性能不通过，不能回写为本轮静态 PASS。

验收人：00 总指挥（待验收）。
