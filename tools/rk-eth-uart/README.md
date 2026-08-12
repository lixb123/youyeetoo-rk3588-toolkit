# RK3588 以太网 UART 联调工具

双击 `start.bat` 启动。默认 UART 参数为 `COM3 / 1500000 / 8N1 / 无流控`。发送前确认 UART 已登录到 RK3588 的 `root` shell。

工具通过 UART 登录后的 Linux shell 控制 RK3588：

- 读取 `youyeetoo_eth_gateway.log`，显示板端实际收到的网口数据、源/目标 IP、接口和优先级。
- 根据界面内容生成协议帧，再让 RK3588 的 Python 通过指定网口发出 UDP 数据。业务源 IP 仅在发送期间以 `/32` 临时加入接口，完成后自动清理。
- 支持 `0xEB90 + APID 0x3D0` 指令、`APID 0x3DF` 文件分段和原始业务透传数据。
- 显示的是应用层数据内容；以太网、IPv4 和 UDP 头由操作系统生成。

首次使用前，应先在 UART shell 中完成板端网口配置：

```sh
cd /home/youyeetoo/youyeetoo_eth_gateway
sudo ./configure_gateway.sh eth0 eth1
```

详细步骤见桌面 `风火轮/RK3588以太网UART联调工具操作说明.docx`。
