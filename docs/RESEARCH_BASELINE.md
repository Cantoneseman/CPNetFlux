# CPNetFlux 现状研究基线

更新时间：2026-09-16

这份文档把旧项目的设计、实现和保留实验结果压缩成长期开发可用的事实基线。它区分“代码已经实现”“实验已经验证”和“仍然只是目标”，避免后续 Agent 把路线图当成事实。

## 项目要解决的问题

CPNetFlux 面向算力网中的大文件、多文件和跨节点传输。总体策略是：控制面兼容实际使用的 GridFTP 风格命令，数据面采用自研 framed protocol；传输引擎、存储适配、manifest 恢复、校验和观测彼此分层。

GridFTP 源码学习得到的主要经验已经体现在设计中：控制命令不直接读写文件，控制面只生成内部任务；数据通道和存储后端隔离；恢复事实以 chunk manifest 为准，而不是把 REST 偏移量当成完整恢复状态。

## 已实现的核心能力

- 单文件 framed STOR/RETR，按 offset 写入。
- 多连接传输和 worker control reuse。
- 临时文件、原子 rename、完成/错误状态帧。
- v2 transfer manifest，支持 verified chunk、缺失 range 和 manifest body CRC。
- CRC32C 软件实现及 x86 SSE4.2 运行时后端选择。
- 下载方向独立 download manifest 和 resume。
- GridFTP 风格控制面：USER、PASS、TYPE、SYST、FEAT、PWD、CWD、CDUP、EPSV、PASV、REST GFID、SIZE、MDTM、LIST、NLST、STOR、RETR 等项目所需子集。
- 目录传输的 tree manifest 和有界 file-level 并行编排。
- token auth、控制面 TLS、STOR/RETR data TLS 的 alpha 路径。
- JSONL 事件日志、阶段指标、release gate 和实验审计脚本。

## 实验已经证明的内容

### Beta 1：公网协议基线

深圳到上海的 bounded 10M 公网环境中，FTP、GridFTP 和 CPNetFlux 均完成真实传输；CPNetFlux worker 版本在小文件和混合目录场景接近 FTP/GridFTP，三次重复均成功，树哈希和 SHA 校验通过。原始 off 版本在小文件密集场景明显慢，worker reuse 修复了主要控制连接开销。

这证明了协议正确性、基本恢复路径和小规模公网可用性；它没有证明 100G 性能。

### Beta 2：连接复用

小文件密集数据上，worker reuse 相比 control reuse off 的加速约为 1.4 到 1.66 倍；单大文件场景收益接近于零，这是合理结果，因为瓶颈已经转移到数据传输。所有保留矩阵的正确性和清理门禁通过。

### Beta 3：压缩 staging

raw、gzip、lz4 在 staging/restore 层的传输、恢复和 tree hash 通过。AI mixed 数据的 gzip 压缩率约 0.812，metadata bundle 约 0.118；HPC science 数据约 1.0，说明收益依赖数据类型。一次旧的 lz4 尾部状态超时误判经过最小复现和重跑修复。

Beta 3 当时 CPSS 环境被阻塞，因此结论是 partial go，而不是完整 CPSS go。

### Beta 4：CPSS 环境解阻

CPSS 虚拟环境、tiny/64MiB roundtrip 和 CPNetFlux/GridFTP CPSS staged transfer 通过。AI mixed CPSS 压缩率约 0.72，metadata 约 0.065，HPC science 约 1.0。FTP CPSS 仍是可选 environment-blocked，不影响 CPNetFlux CPSS 核心结论。

### Beta 5：报告收口

Beta 5 只是汇总 Beta 1-4 的证据，没有新增性能矩阵。它支持“CPSS-aware go review-ready”，不支持生产 readiness、50G/100G readiness 或重型长期稳定性结论。

## 不能从现有结果推出的结论

- 没有完成 10GiB、20GiB、100GiB 或 heavy soak 验证。
- 没有完成稳定 100G 专线端到端验证。
- 没有实现生产级 GSI、凭据生命周期、委派、撤销和策略引擎。
- 默认仍是 anonymous、TLS off、POSIX backend；alpha 安全配置不能当作生产安全方案。
- STOR/RETR 数据通道使用 CPNetFlux framed protocol，不是普通 FTP raw data stream。
- 目录传输不保留空目录、权限、owner/group、xattr、ACL、硬链接和 symlink target。
- 压缩仍是 staging/restore 编排层，不是 CPNetFlux C++ 热路径能力。
- 当前实验环境公网带宽约 10M，不能支撑高带宽性能声明。

## 关键工程经验

1. 小文件场景必须复用控制连接；否则协议控制开销会吞掉数据通道收益。
2. 恢复状态必须由 manifest、verified chunks 和 checksum 共同定义；最终 SHA 适合验收，不适合作为恢复事实源。
3. 压缩要按数据类型讨论，不能用单一平均压缩率代表科学数据。
4. 测试结果必须同时保存 transfer status、restore/tree hash、cleanup audit 和环境信息。
5. 历史报告中的“go”是阶段性门禁结果，不是产品级承诺。

## 新工作区的开发边界

当前 CPNetFlux 工作区保留了旧版本的未提交实现和实验性 scheduler/compression 代码，但它们没有自动升级为正式路线。下一步应先做一次代码审查和架构分叉：

- 保留：已通过单元测试和 Beta 正确性门禁的传输、manifest、checksum、控制面能力。
- 重新评估：global scheduler、hot-path compression、复杂性能 knob。
- 单独规划：生产认证、观测后端、100G 专线、raw FTP compatibility。

所有新的 Agent 任务都应引用本基线，并明确哪些结论是历史证据、哪些是新假设。

