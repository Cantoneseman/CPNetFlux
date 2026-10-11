# AUTH-FOUNDATION-01 / AUTH-POLICY-01：认证与访问策略基础

日期：2026-10-11
权威输入提交：`66c66e42c9c1aa8a10334d271fec3ddbb11ae80f`
实施目录：深圳 CPNetFlux 隔离 worktree `/tmp/cpnetflux-runs/AUTH-FOUNDATION-01/src`
分支：`codex/AUTH-FOUNDATION-01`

## 目标和范围

**AUTH-FOUNDATION-01** 为现有 TLS/token 登录补上双向 TLS 客户端证书校验，客户端可配置证书和私钥；配置 CA 时验证服务端 DNS/IP 身份；限制 token 文件为安全打开的普通文件且最多 4096 字节，并保持凭据比较不因首个不匹配字节提前退出。控制连接仍先做 TLS，再由 token 完成应用层登录。

**AUTH-POLICY-01** 增加服务级 `read-write`、`read-only`、`write-only` 策略。策略覆盖普通 FTP 控制命令与 `XDIR/XDIRP` 目录命令；`REST` 本身不判为写操作，以保留只读下载续传；实际 `STOR` 与 `RETR` 分别执行写和读授权。根目录路径解析拒绝穿过已存在的符号链接。

## 非目标

本阶段不实现账号数据库、证书到用户的身份映射、每用户/每目录 ACL、证书签发轮换、GSI/DCAU/PROT、桌面端、Linux 安装器或 TLS 显式升级。现有 anonymous 兼容模式仍在；它不适用于真实部署。V2 持久目录数据连接的加密不在本阶段内。

## 验收标准

- mTLS 服务端拒绝无客户端证书的连接；信任 CA 签发的客户端证书；已配置 CA 的客户端拒绝错误服务端主机名。
- token 模式仍正确拒绝错误 token、接受正确 token；token 文件符号链接、宽权限、空文件和超长内容均拒绝。
- read-only / write-only 正确覆盖文件传输、元数据/目录访问及 XDIR/XDIRP；read-only 仍允许 RETR resume。
- 新客户端证书参数在树传输、文件上传和下载参数解析中可用。
- 完成定向测试、完整 CTest 和 public-hygiene 门禁；记录提交 SHA、push 结果及 GitHub 远端 SHA 回读。

## 允许修改路径

`CMakeLists.txt`、`docs/SECURITY.md`、本任务记录、`docs/coordination/BOARD.md`；
`include/cpnetflux/core/io/tls_socket.h`、`include/cpnetflux/protocol/control/control_auth.h`、`include/cpnetflux/protocol/control/control_command.h`、`include/cpnetflux/protocol/control/control_options.h`；
`src/config/file_download_options.cpp`、`src/config/file_transfer_options.cpp`、`src/config/tree_transfer_options.cpp`；
`src/core/io/framed_data_socket.cpp`、`src/core/io/tls_socket.cpp`、`src/core/io/tls_socket_stub.cpp`；
`src/protocol/control/control_auth.cpp`、`src/protocol/control/control_command.cpp`、`src/protocol/control/control_options.cpp`、`src/protocol/control/control_server.cpp`；
`tests/unit/control_auth_test.cpp`、`tests/unit/control_options_test.cpp`、`tests/unit/control_session_test.cpp`、`tests/unit/file_download_options_test.cpp`、`tests/unit/file_transfer_options_test.cpp`、`tests/unit/tree_transfer_options_test.cpp`、`tools/test/run_gridftp_control_mtls_smoke.py`, `tools/test/run_gridftp_data_tls_smoke.py`.

## 回滚方式

只回滚本任务在 `codex/AUTH-FOUNDATION-01` 中产生的提交（按依赖顺序先回滚策略提交，再回滚基础提交）；不要 reset、clean 或切换深圳主工作区。部署端撤销新增的 mTLS/访问策略参数并退回到上一个已验收提交。任务 token、测试证书和构建目录均为临时运行资料，不进入提交。

## 验收记录

- TLS build: `cmake -S . -B /tmp/cpnetflux-runs/AUTH-FOUNDATION-01/build -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Release`; `cmake --build ... -j2` passed.
- Focused unit/policy/mTLS smoke passed; full `CPNETFLUX_TEST_TOKEN=<temporary> ctest --test-dir /tmp/cpnetflux-runs/AUTH-FOUNDATION-01/build --output-on-failure` passed: 257/257, one conditional io_uring test skipped because the runtime backend is unavailable.
- TLS-off compatibility build `-DCPNETFLUX_ENABLE_TLS=OFF` passed; 9 focused auth/policy/option tests passed.
- `python3 tools/release/test_public_hygiene.py` passed; `git -c core.whitespace=cr-at-eol diff --check` passed.
- The existing data-TLS smoke certificate fixture was updated with `subjectAltName=DNS:localhost,IP:127.0.0.1` so hostname/IP verification tests the intended behavior.

状态：已完成，实现、单元、集成与兼容性门禁已通过。
