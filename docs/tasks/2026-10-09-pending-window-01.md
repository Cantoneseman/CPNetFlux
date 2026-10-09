# DIR-V2-PENDING-WINDOW-01

目标：在已存在的持久目录数据会话上增加有界 pending window，使文件 N+1 的帧发送可与文件 N 的 FILE_RESULT 等待重叠，降低 128×1MiB 目录的逐文件往返损耗。

输入：深圳 CPNetFlux 分支 codex/DIR-V2-TRANSFER-ENGINE-01，HEAD 5cd2fb69232a860d9dd0000b631a0c1123cc8324。

非目标：不改变 v1 协议、manifest/resume/hash 语义、默认配置、scheduler、TLS、checksum 或云端历史目录；不启动长矩阵。

允许修改：持久目录数据会话接口/实现、树传输选项与控制协商、控制服务兼容解析、定向单元/loopback 测试、128×1MiB 短实验脚本与本任务结果文档。

验收：默认窗口 1 行为兼容；窗口 2/4 使用单数据连接和 bounded queue；身份/generation 严格匹配，断线/超时/写失败能回收线程并返回错误；CMake/CTest 和定向 smoke 通过；128×1MiB upload/download 记录窗口 1/2/4 的 elapsed/吞吐及 hash/manifest。

交付：仅提交本任务明确文件，push 到已存在 GitHub 分支并回读远端 SHA；长实验用脚本执行，不轮询。
