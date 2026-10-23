# V3 range checkpoint 合并
输入提交：c9b96331eefa1b04037513658e0578559f5c5f3a。
范围：只减少 range 路径已验证块的 per-file manifest 原子保存频率；逐块数据 flush、CRC、租约、提交与 V3 格式保持。1/4/16 块单变量实验；100 ms 最早脏块定时触发；范围结果、连接正常停止/错误及文件提交强制补齐。后台失败保持粘性并禁止成功。
非目标：本轮不拆 Entry 锁、不增大 pipeline、不改网卡/TLS/CRC、不宣称断电安全或最终硬件达标。
设计：每批 context 共用一个惰性定时写者；任务使用 weak Entry 与 epoch，队列锁不跨 Entry 锁。Entry 锁仍串行保护 writer/session；后台使用自有统计并在前台边界归并，绝不保留 lane 栈指针。range 批量模式显式延后 session 的重复自动保存。数据仍逐块 flush 后才加入可恢复记录。强杀丢失未落盘记录，恢复只信磁盘 checkpoint。
受影响：persistent_tree_transfer.cpp/.h；TransferSession/DownloadSession；persistent_data_session_test.cpp；range observation smoke/perf harness；CMakeLists.txt；本任务证据及性能路线。
验收：先用现有二进制验证新行为测试为 RED；阈值、空闲定时、范围结果强制 flush、断开、保存失败粘性、双向 SIGKILL 恢复与独立 SHA；专项与完整 CTest；512 MiB TLS/CRC 四路 >=5 组交错 batch1/4/16，记录真实保存数、wall 和恢复窗口。仅稳定 >=10% 收益才采用新默认；100 ms 是触发时间，非慢盘完成承诺。
交付：深圳原目录单人修改；固定提交同步上海并构建核对二进制；GitHub push 回读；更新 Windows 性能优化路线.md。
