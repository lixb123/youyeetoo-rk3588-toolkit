# 风火轮 RK3588 工程与验证工具集

这是我在 Youyeetoo RK3588 平台联调中整理的可复用工程成果，重点解决三个问题：应用侧相机与 CAN/遥测任务如何组织，双网口协议如何做闭环验证，以及没有 SSH 时如何通过 UART 安全部署和定位问题。

仓库只收录我维护的源代码、测试流程代码、Codex skills/agent 约束和操作说明。厂商 CameraSDK、LubanCat/Rockchip SDK、固件与镜像、原始需求文档、现场日志、测试记录、密钥和构建产物均未收录。

## 我完成了什么

- 在 `app/` 中实现 RK3588 应用的 CameraSDK 适配、多相机按唯一序列号路由、命令调度、worker IPC、CAN、遥测及看门狗相关逻辑。
- 建立可回滚的 UART 应用迭代流程：打包、串口接收、安装前备份、SHA-256 校验、服务健康检查和失败回滚。
- 在 `gateway/` 中实现板端双网口 UDP 网关和接口配置脚本。
- 在 `tools/eth-sim/` 中实现 Windows 双网卡的 NB-IoT 载荷与 X 测控模拟器、协议编解码和单元测试。
- 在 `tools/rk-eth-uart/` 中实现通过 UART 控制板端收发以太网协议帧的联调工具。
- 在 `tools/rk-cam-uart/` 中实现相机命令控制台，支持设备枚举、唯一序列号选择和顺序查询。
- 在 `skills/` 与 `AGENTS.md` 中固化 Agent A/B/B2/C/D 的职责边界、构建交接、随机协议向量、以太网闭环验证、相机验证、发布镜像与板卡验收流程。

## 仓库结构

```text
app/                  RK3588 主应用与 UART 开发部署脚本
gateway/              板端双网口 UDP 网关
tools/eth-sim/         Windows 双网卡载荷/X 测控模拟器
tools/rk-eth-uart/     Ethernet-over-UART 联调工具
tools/rk-cam-uart/     CameraSDK-over-UART 控制台
skills/                可安装的 Youyeetoo Codex skills
scripts/               仓库级本地自检
AGENTS.md              工程角色、交接和硬件安全规则
```

## 环境要求

本地协议测试只需要 Windows 10/11、PowerShell 5.1 或 7，以及 Python 3.10+。图形工具使用 Python 标准库；串口工具需要 PowerShell 可访问目标 COM 口。

板端构建需要 AArch64 交叉工具链、RK3588/LubanCat Linux 环境和合法获得的 Insta360 CameraSDK。CameraSDK 头文件与 `libCameraSDK.so` 必须由使用者自行放置，仓库不会提供或下载它们。

硬件闭环需要 RK3588 板卡、USB 相机、串口适配器，以及按测试拓扑准备的两块 Windows 以太网适配器。默认地址和接口名只是实验室示例，运行前应按现场网络调整。

## 先跑本地测试

在仓库根目录执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\run_local_tests.ps1
```

如果 `python` 仍指向 Windows 商店占位符，可显式指定解释器：

```powershell
.\scripts\run_local_tests.ps1 -PythonPath 'C:\Path\To\python.exe'
```

它会运行两组协议测试、编译检查全部 Python 文件，并阻止常见 SDK 二进制、镜像和私钥混入仓库。

也可以单独运行：

```powershell
python -m unittest discover -s .\tools\eth-sim\tests -p 'test_*.py' -v
python .\tools\rk-eth-uart\test_protocol.py
```

## 复现双网口闭环

1. 将 RK3588 的载荷口和 X 测控口分别连接到 Windows 的两个物理网口。
2. 用管理员 PowerShell 运行 `tools/eth-sim/setup_interfaces.ps1`，按实际适配器名称传参。
3. 在板端编译并启动网关程序，显式指定载荷接口和 X 接口；板端现有可执行文件名保持兼容。
4. 启动 `tools/eth-sim/nb-iot-payload/payload_sim.py` 与 `tools/eth-sim/x-control/x_control_sim.py`，先做单包，再做多包和文件分段测试。
5. 对没有 SSH 的现场环境，使用 `tools/rk-eth-uart/rk_eth_uart.py` 经 UART 检查板端日志并发起反向数据测试。

具体参数与命令见 `tools/eth-sim/README.md` 和 `tools/rk-eth-uart/操作说明.md`。

## 复现相机应用迭代

日常应用开发采用可恢复链路：

```text
Agent A-CAM -> UART development bundle -> Agent D-VAL
```

1. 在合法 CameraSDK 和 AArch64 工具链环境中构建 `app/`。
2. 运行 `app/scripts/build_uart_dev_bundle.sh` 生成开发包。
3. Windows 端运行 `app/scripts/uart_deploy_camera.ps1`，通过 UART 传输。
4. 板端安装脚本会备份当前二进制和 SDK 库、核对 SHA-256、重启服务并执行健康检查；失败时自动回滚。
5. 使用 `tools/rk-cam-uart/` 顺序执行设备枚举、`CAMERA_INIT`、状态、电量、存储和媒体查询。多相机必须用唯一序列号路由。

如果 UART 连续在不同分帧超时或进入 RK3588 FIQ 调试器，应停止大文件串口重试，保留 UART 作为恢复控制台，临时使用已经 Link Up 的管理网口传输同一个开发包。板端 SHA-256 必须与电脑端一致，安装仍使用同一套备份、健康检查和自动回滚脚本；验证完成后关闭临时文件服务并删除暂存包。

`dispatch_ok` 只表示命令被路由接受。验证成功必须同时看到目标 USB 设备、正确链路速率、最新 `connected=yes` 和 worker 完成结果。相机从 USB 总线消失时应停止查询并检查硬件链路。

## 安装 Codex skills

将所需目录复制到个人 Codex skills 目录，然后重新打开 Codex：

```powershell
Copy-Item -Recurse .\skills\youyeetoo-* "$env:USERPROFILE\.codex\skills\"
```

入口 skill 是 `$youyeetoo-handover`。网络、相机、协议审计、随机向量、镜像发布和板卡验证由对应的窄 skill 接管。`AGENTS.md` 规定了项目级安全边界。

## 安全边界

- 不要把密码、令牌、私钥、CameraSDK 二进制、现场日志或原始任务文档提交到仓库。
- 不要把应用 UART 部署流程用于 U-Boot、kernel/DTS、rootfs、分区或启动链变更。
- 不要在没有明确授权时刷写硬件或覆盖唯一已知可用镜像。
- 对真实设备执行变更前，先确认目标、备份当前版本并记录哈希。

本仓库默认作为个人工程归档使用，不包含开源许可证；第三方使用与再分发需另行确认授权。
