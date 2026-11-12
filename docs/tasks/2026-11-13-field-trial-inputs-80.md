# 阶段80：实验启动输入闭合

状态：输入修正及R1无副作用门通过，R2门与新正式实测待执行。唯一writer root，79 serial-return已读且writer_released=true。

目的：避免在RX改变后才发现校准plan、旧preflight文件名和本地imports丢失。verify_experiment_inputs.py只读AST/路径/哈希，要求客户端和校准plan一致并校验4个channel/独立队列；可用冻结JSON记录所有必需脚本、计划、双端版本收据及交接。实验controller/run入口先运行该门，再允许副作用。输入hash后修改脚本必须重新冻结并重验双端，不能沿用旧pass。

真实RED：79缺ring-control/receiver-full.plan且preflight77.py不存在，检查拒绝；80迁移遗漏common79/guard79/ring79等import同样先拒绝。修复后18Python文件及计划输入通过；冻结合同副本删除计划必拒绝。R1时钟4/4、独立回滚12/12通过。原79输入/证据不修改。

证据：/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-scenarios-80。验收命令：python3 -B verify_inputs.py ROOT --contract ROOT/input-contract.json；python3 -B test_clock_domain.py；python3 -B ring-control/test_guard80.py。R2复制输入后执行同门并保存真实结果，所有阶段脚本准备好后才允许新90分钟窗口。原79窗口已结束，不续用。

后续：新版本1GiB准入、五对CP/Grid、dense/tiny/mixed各3、反向/安全→>=1800秒且>=100个跨机任务→真实故障与清理。任何硬门失败停止回滚并交还。1GiB/内存预算保持；产品、rate/lane/RSS/IRQ、原生RDMA优化不变。当前仍无新吞吐/稳定性成绩，上机NOT_READY。
