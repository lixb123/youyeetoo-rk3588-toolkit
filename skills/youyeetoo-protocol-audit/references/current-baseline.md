# Current Protocol Audit Baseline

## Recent Source Set

Recent work compared project PDFs, DOCX, XLSX, Markdown summaries, and demos covering NB-IoT, Ethernet, X telemetry/control, X data transmission, and the RK3588 board.

Relevant local material includes:

- `<project-docs>\风火轮_NB-IoT2.0_can通信协议_20260810.pdf`
- `<project-docs>\NB-IoT载荷以太网通信需求及 IP 规划 0806.docx`
- `<project-docs>\星移联信NB2.0星载基站与卫星平台交互流程设计文档 0810.docx`
- `<project-docs>\NB测试表2.0_对外0810.xlsx`
- `<project-docs>\以太网通信协议 星移.docx`
- `<workspace>\youyeetoo_eth_gateway\需求重新核对.md`
- `<workspace>\rk3588_eth_demo\需求总结.md`

## Known Classification

- Board development target is RK3588; older material may say RK3568 and must be flagged.
- Current development board exposes `eth0` and `eth1`; a final three-port hardware implementation is not closed.
- `10.240.1.34/24` and UDP `47000` are demo selections, not confirmed formal allocations.
- The application protocol uses big-endian multi-byte fields and documented identifiers/checksum rules, but complete port, retry, MTU, flow-control, and failure-recovery definitions remain incomplete.
- Small UDP two-link tests passed; this is not evidence of final throughput, long stability, three-link concurrency, or ICD completion.
