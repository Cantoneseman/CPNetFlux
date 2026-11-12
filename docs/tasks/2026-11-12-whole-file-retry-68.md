# 阶段68：非分片文件租约迁移后的恢复握手

输入实现839352cd8273c8badf52d4b2da58fa784c4780c8，阶段67证据目录/home/Su/experiments/CPNetFlux/2026-11-12-tcp-source-plan-67。根目录单人串行；R2停止源码写入，仅后续固定提交验收。

范围：阶段67早期512B注入捕获identity mismatch及原total deadline timeout。协议trace证实非range文件attempt1被健康sender channel接走，而resume目前取决于channel options.resume；新接手worker不会发kFileResume。接收端因此无法执行正确缺失集握手/恢复检查点。另需验证显式lane reconnectAttempt=0是否覆盖queue权威租约attempt。先为实际sender/receiver和有4B已验证检查点的8B文件添加失败回归；对upload/download和range/whole覆盖，用健康worker接管、全局resume=false，验证实际只补4B、attempt单调、内容一致、队列清空。不得放松身份检查/CRC、增加deadline或盲重试。

受影响：tests/unit/persistent_data_session_test.cpp，src/core/io/persistent_tree_transfer.cpp；必要时tools/test/run_cpnetflux_source_port_plan_smoke.py的早期边界覆盖与证据保留；本计划/结果/README/docs入口。若身份不匹配另有原因，保留开放并继续定向归因，不用测试阈值调整冒充修复。

非目标：不动RDMA/系统网络、默认帧/pacing/吞吐配置，不开始正式GridFTP或soak，不扩大跨机payload。本阶段先loopback；跨机只有提交同步同配置四hash一致后另立合同。

验收：新确定性实际socket sender/receiver回归先RED，再最小修复GREEN；相关PersistentRecovery/Lease/Lifetime/Session、source-plan与既有lane recovery/peer mapping/timeout/large-data门；保留原512B边界日志，最多三对带trace定向复核，遇新失败停止并保留按case证据。R1提交推送后R2同commit构建/相关CTest/四hash。更新阶段67候选仍OFF、正式比较NO-GO/稳定性未运行/上机未就绪的边界，记录全部失败，不把本阶段替代完整交付C。
