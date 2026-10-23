# V3-FINAL-DUALHOST-ACCEPTANCE-01 结果

日期：2026-10-23  
输入提交：9f0dc540582857d17d5456d14bbe85eebb8b07ed  
代码基线：summary CRC reuse 阶段，深圳与上海三项候选二进制 SHA 一致。

## 结论

CPNetFlux V3 已在两台阿里云服务器之间完成真实跨机双向数据流验收：

- 深圳客户端 → 上海候选 V3 服务：上传通过。
- 上海候选 V3 服务 → 深圳客户端：下载通过。
- 两次传输均使用公网地址、TLS 控制连接和 TLS 数据连接，不是 loopback 模拟。
- 4 条数据链路、persistent_tree、CRC32C 和 verified-chunks 摘要均被实际摘要确认。
- 源端、上海落盘 payload、深圳下载 payload 的逐文件 SHA-256 一致。

软件验收通过；上海客户端直接连接深圳 2121 的反向拓扑未通过，原因是深圳云安全组对入站 2121 超时。该失败发生在 TCP 建连前，摘要为 reconnect attempt budget exhausted: connect: Connection timed out，不属于 CPNetFlux 协议或数据校验失败。

## 实际测试数据

测试树共 4 个文件：

- payload-160MiB.bin
- nested/payload-3MiB.bin
- readme.txt
- empty.dat

总字节数：170,917,922。源端 SHA 清单位于深圳 /tmp/cpnetflux-dualhost-01/source.sha256。

上传（深圳 → 上海）：

- 控制端点：47.116.174.181:2121
- 上海临时根目录：/tmp/cpnetflux-dualhost-01/shanghai-root
- 结果：pass
- TLS：control_tls_verified=true，data_tls_verified=true
- 传输：actual_transport=v3，actual_mode=persistent_tree
- 数据链路：4
- 用时：15.5112 s
- 实测摘要吞吐：0.08815 Gbps
- 摘要树哈希来源：verified_chunks

下载（上海 → 深圳，深圳客户端连接上海服务）：

- 控制端点：47.116.174.181:2121
- 上海源目录：upload-01
- 深圳落盘目录：/tmp/cpnetflux-dualhost-01/downloaded
- 结果：pass
- TLS：control_tls_verified=true，data_tls_verified=true
- 传输：actual_transport=v3，actual_mode=persistent_tree
- 数据链路：4
- 用时：13.8643 s
- 实测摘要吞吐：0.09862 Gbps
- 摘要树哈希来源：verified_chunks

下载目录中的 .cpnetflux.download.manifest 是客户端断点元数据；排除这些元数据后，4 个 payload 的 SHA-256 与源端完全一致。上海上传目录中的 .cpnetflux.manifest 同理不计入 payload 比对。

## 构建一致性

深圳和上海候选构建 SHA：

- cpnetflux-gridftp-server: 027da66ecfab21f6227776507067666317ee0f8644cb760b29524406b29b4b67
- cpnetflux-tree-upload-client: c03b7f84acfb6aeb067cb8c1529e989f30889044947aeb026ccba069ca60b66a
- cpnetflux-tree-download-client: 0b7e71ac012ba6e38ce387312d4fae80c75e29f26794757c03fcaa3488bf9303

两端 /usr/bin/cpnetflux-gridftp-server 旧服务均已恢复，服务命令和运行根目录未改变。

## 验收命令

深圳上传：

cpnetflux-tree-upload-client --host 47.116.174.181 --port 2121 --source-dir /tmp/cpnetflux-dualhost-01/source --dest-dir upload-01 ... --tls-mode required --tls-server-name cpnetflux-shanghai --tls-ca-file /tmp/cpnetflux-dualhost-ca.crt --data-tls-mode required

深圳下载：

cpnetflux-tree-download-client --host 47.116.174.181 --port 2121 --source-dir upload-01 --dest-dir /tmp/cpnetflux-dualhost-01/downloaded ... --tls-mode required --tls-server-name cpnetflux-shanghai --tls-ca-file /tmp/cpnetflux-dualhost-ca.crt --data-tls-mode required

两条命令的完整 JSON 摘要分别为：

- /tmp/cpnetflux-dualhost-01/upload-summary.json
- /tmp/cpnetflux-dualhost-01/download-summary-shenzhen.json

## 后续

1. 软件 V3 跨机核心闭环已完成，本轮没有发现需要修改的生产代码。
2. 若需要“上海客户端 → 深圳服务端”的对称网络拓扑验收，下一步只需由运维放通深圳入站 2121 和 20200–20711；这属于云安全组配置，不属于本轮代码开发。
3. 物理网卡绑定验证继续按既定计划暂缓。
