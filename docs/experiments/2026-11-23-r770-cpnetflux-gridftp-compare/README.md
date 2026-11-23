# 2026-11-23 R770 对比复现说明

实验报告位于 /home/Su/projects/CPNetFlux/docs/tasks/2026-11-23-r770-cpnetflux-gridftp-compare-result.md，原始证据根目录位于 /home/Su/experiments/CPNetFlux/2026-11-23-r770-cpnetflux-gridftp-compare-01。evidence-final.json 列出原始文件的 SHA-256。summary.json 只汇总通过门槛的样本，同时列出拒收项。

reproduce 中为当前恢复窗口实际用过的控制脚本、节点脚本和分析脚本快照。Git 中的复现脚本使用 LF 换行，现场快照保留实验时的原始字节。控制器运行于管理机，通过免密 SSH 调用两端节点脚本，每次只启一个传输。节点脚本使用固定的本轮运行根目录、端口与实验凭据路径。原目录保留证据且其临时凭据已经清理。复测先创建独立目录、调整脚本的路径常量、核对双端构建和空载计数，再配置新的短期测试凭据和受限临时服务。此脚本不会创建或配置证书、GridFTP 服务、路由或系统网络。

正式主对比使用以下参数调用 annual-controller.py（参数为 run ID、字节数、发送端设备编号和串行模式列表）：

    python annual-controller.py main-formal-9gb-r2-r1 9000000000 2 cp:4 grid:4 grid:4 cp:4 cp:4 grid:4 grid:4 cp:4 cp:4 grid:4
    python annual-controller.py main-formal-9gb-r1-r2 9000000000 1 grid:4 cp:4 cp:4 grid:4 grid:4 cp:4 cp:4 grid:4 grid:4 cp:4
    python secure-controller.py sec-formal-8gb 8000000000 2 grid:4 cp-tls:4 grid-nongsi:4 cp-clear:4 grid-private:4 cp-clear:4 grid-private:4 grid:4 grid-nongsi:4 cp-tls:4 grid-nongsi:4 grid:4 grid-private:4 cp-tls:4 cp-clear:4

两种控制脚本遇到失败会结束本次运行，不做自动缩档。手动确认现场恢复后，才能用新 run ID 和新尺寸调用。GridFTP 的受限服务实际命令、脱敏回执、二进制版本、原始数据和样本环境快照均在证据目录。通过 python3 summarize-annual.py 在 R770-1 复算汇总。
