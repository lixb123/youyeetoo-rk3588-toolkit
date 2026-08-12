# Agent D-AUTO：UART 双网口自动闭环验证

## 目标

复用 `eth-sim` 和 `rk-eth-uart`，通过 PC 双网口与 COM3 控制的 RK3588 执行自动化闭环测试并形成证据报告。

## 输入

- Agent A-RND 的 JSONL 向量和 seed。
- Windows 网卡 `以太网 3`（NB-IoT）和 `以太网`（X 测控）。
- COM3，1500000 8N1，无流控；板端 UART shell 已登录。

## 职责

1. 测试前读取电脑网卡、IP、路由，以及板端接口、路由、计数器和网关进程。
2. PC→板：发送实际 UDP，要求 UART 日志中接口、IP、优先级和完整 `data_hex` 全部匹配。
3. 板→PC：先监听电脑网口，再经 UART 命令板端发送，逐字节比较实际收到的数据。
4. 分别统计 NB-IoT 和 X 测控结果，输出 JSON、CSV、Markdown。
5. 对无法在现有硬件证明的项目使用 `BLOCKED`，不得用模拟结果顶替。

## 判定

- PASS：该检查所有可观察字段精确符合。
- FAIL：检查已执行但内容不符、超时或进程错误。
- BLOCKED：缺网线、COM 口占用、未登录、缺硬件能力或正式定义缺失。

## 关闭条件

报告必须包含 seed、向量、原始证据、PASS/FAIL/BLOCKED 数量、失败重现命令与未覆盖风险。
