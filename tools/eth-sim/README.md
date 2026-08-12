# eth-sim 电脑网口模拟测试

本目录是电脑端测试程序，固定按物理网口角色拆分：

| 电脑端 | 模拟对象 | 开发板端 | 程序目录 |
|---|---|---|---|
| 网口0 | 窄带物联网载荷 | `eth0` | `nb-iot-payload` |
| 网口1 | X 测控机 | `eth1` | `x-control` |

两个程序都使用 Python 标准库，双击各自目录的 `start.bat` 即可打开测试界面；也可以在 PowerShell 中直接运行脚本的命令行入口。公共协议编解码位于 `common/youyeetoo_protocol.py`。

## 首次配置电脑网卡

在管理员 PowerShell 中执行，默认适配器名是电脑“以太网 3”（网口0）和“以太网”（网口1）：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
cd C:\path\to\eth-sim
.\setup_interfaces.ps1
```

如果 Windows 中的适配器名称不同，显式传入名称：

```powershell
.\setup_interfaces.ps1 -PayloadAdapter '以太网 2' -XAdapter '以太网 4'
```

清理本次模拟地址和路由：

```powershell
.\setup_interfaces.ps1 -Clear
```

脚本配置网口0 的 `10.240.1.35/.37/.38/.39` 和网口1 的 `10.240.1.2/30`。为避免网口0 的 `/24` 地址影响 X 测控，`10.240.1.36/32` 通过开发板 X 口 `10.240.1.1`，载荷业务和管理目标通过开发板载荷口 `10.240.1.34`。

## 窄带物联网载荷界面

运行 `nb-iot-payload\start.bat`。界面可选择 N6 业务、基站管理/日志、核心网管理/日志和 S1 通道，填写文本或十六进制数据、次数、间隔后发送，并可在网口0 的本地地址监听 UDP 47000。

命令行示例：

```powershell
python .\nb-iot-payload\payload_sim.py send --source-ip 10.240.1.38 --target-ip 10.240.1.50 --text 'N6 business test' --count 5 --interval 0.2
python .\nb-iot-payload\payload_sim.py listen --bind-ip 10.240.1.38 --count 5
```

## X 测控机界面

运行 `x-control\start.bat`。交互指令页支持链路检测、版本查询、重构、FTP、星历等指令；文件分段页按每段最多 1000 字节生成 `0x3DF` 文件包；接收监听页可检查板端应答。板端监视器默认只串口打印，不依赖电脑端界面返回应答。

命令行示例：

```powershell
python .\x-control\x_control_sim.py command 001D --wait
python .\x-control\x_control_sim.py command 0101 --params '01 02'
python .\x-control\x_control_sim.py file .\sample.bin --interval 0.05
```

## 与开发板程序联调

开发板端仍只运行一个程序 `youyeetoo_eth_gateway`，同时监视 `eth0` 和 `eth1`，无需图形界面。程序启动后会把所有串口标准输出继续打印，并追加写入与可执行文件同级的 `youyeetoo_eth_gateway.log`。例如：

```sh
sudo ./youyeetoo_eth_gateway --payload-iface eth0 --x-iface eth1
```

板端程序的日志文件不写入当前工作目录或 `/var/log`，升级或复制程序时可直接连同同级日志一起定位测试记录。
