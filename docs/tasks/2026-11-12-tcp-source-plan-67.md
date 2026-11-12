# 阶段67：有界TCP源端口计划候选

输入d9567fa9aca7a44660ff1615cd1168e9799b4dd2。阶段66独立4流同RQ可触发OOB；原CP逐流因果和正式比较仍开放。本阶段实现准备候选，不改默认性能、协议、网卡、pacing或接收后端；单人R1根目录写入，不派子代理/worktree。

设计：新增下载专用显式 --tcp-data-source-port-plan <path>，仅单link、显式唯一IPv4 local/peer、TCP；控制连接不使用候选端口。由离线Python生成器读取ethtool RSS/fields快照，生成带local/peer/channel-count/配置SHA和目标queue元数据的有界文本计划。key/table和算法不进入C++热路径；只在连接准备使用(channel,被动peer-port)对应候选端口列表。不得宣称端口计划能强制硬件队列；调用者必须核对当前配置与生成快照，并结合实际tuple/CPU/NAPI/计数验收。

固定文本v1头：CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1 <local-ip> <peer-ip> <channels> <rss-snapshot-sha256>；行：<channel> <peer-port> <target-rx-queue> <source-port>...。channel 1..16；文件<=1MiB、最多2048行，每行最多128个候选，总候选<=65536，端口1024..65535（peer1..65535），queue0..4095；唯一(channel,peer-port)，行内源端口不重复；不同channel目标queue必须不同且各channel跨peer-port目标一致。源/peer地址必须与实际数字IPv4参数一致。缺失行/格式错误/范围错误/空表/重复不允许。snapshot SHA是来源证据，非运行时设备校验；不得冒充实时保证。

运行时：immutable计划在任务启动解析一次；同(channel,peer-port)候选顺序从首项扫描，socket bind成功才连接；只因EADDRINUSE跳过，不使用SO_REUSEADDR/SO_REUSEPORT，不绕过TIME_WAIT。连接失败、deadline或TLS错误不扫描成网络重试；现有lane恢复重新EPSV并按新peer-port查表。耗尽/缺失行/过期deadline明确失败，不回退ephemeral。候选检查最多128次且每步检查任务deadline。计划仅源端口选择，已有重试/取消和控制面语义保持。成功端口通过既有实际tuple观测；增加每channel计划目标queue和bind尝试数、全局计划snapshot SHA元数据。默认无计划路径不变；RDMA/IPv6/上传/多link/缺少显式地址拒绝。以现有mmap实验组合时不改变其他约束。

受影响：新增include/cpnetflux/core/io/tcp_source_port_plan.h、src/core/io/tcp_source_port_plan.cpp；socket_utils.{h,cpp}、framed_data_socket.{h,cpp}、tree_transfer_client.cpp、tree_transfer_options.{h,cpp}、CMakeLists.txt；tests/unit/tcp_source_port_plan_test.cpp、socket_bind_test.cpp、tree_transfer_options_test.cpp；新增 tools/perf/generate_cpnetflux_tcp_source_plan.py及对应Python行为测试、tools/test/run_cpnetflux_source_port_plan_smoke.py。文档本计划/结果/入口更新按实际完成。保留现有两份无关untracked docs。

验收：先新CLI端到端行为测试对旧程序观察unknown-option RED；再实现。独立socket实际accept端口验证：首候选占用时换下一个、全占用失败、过期deadline无连接、IPv4地址不符拒绝；计划parser覆盖格式/预算/重复/错误channel和跨channel同queue；生成器使用阶段65独立hash oracle与真实snapshot的校验，未知hash模式拒绝。完整V3少量loopback文件TLS下载+连续3任务、data-reset/control-reset重连、耗尽/缺失行明确失败且无ephemeral路径，内容hash正确、JSON实际端口在计划范围。接入CMake/CTest，相关options/socket/protocol/recovery/large-data门通过。

只有R1明确提交/推送、R2同提交同配置四目标构建/hash核对后才允许一次有界跨机小流/零或<=1MiB文件的计划tuple验证（最多4数据channel）；先新计划准确映射，不启动新1GiB/8GiB性能负载。资源MemAvailable>=12GiB、owned swap0、host pswpout无新增、临时目录和服务按身份清理，不动2121或旧数据。最终代码/证据冻结/推送；网络/正式比较/稳定性仍不因此通过。

后续：候选功能闭环后，新合同只允许有限同条件完整CP文件验收，并记录必要plan生成/配置核验和全部任务成本；网络门通过后续接原配置和实验聊天正式GridFTP配对，再稳定性>=1800s且>=100任务及恢复/取消/TLS/资源趋势，最后给上机结论。不得将本工程交付替代用户完整目标。

实现核对后的预算修订（正式候选前）：服务端实际passive窗口为512端口，4channel×512×128=262144候选。为覆盖完整窗口和连续任务，不截断至16/128个remote端口：文件上限修订为2MiB，总候选262144（行数仍2048、每行128）。生成器按channel切分源端口区间，使不同channel不会因源端口池重合相互占用；它不改变接收队列或内核TIME_WAIT语义。端口耗尽仍必须失败；后续100任务稳定性需实测，不以容量推导替代。
