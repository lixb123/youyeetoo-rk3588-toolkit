# Validation Boundary

- PC adapter `以太网 3` simulates the NB-IoT payload using `.35/.37/.38/.39`.
- PC adapter `以太网` simulates X telemetry/control at `10.240.1.2/30`.
- Board `eth0` is the payload link; board `eth1` is the X telemetry/control link.
- The current gateway observes incoming frames with AF_PACKET and ignores `PACKET_OUTGOING`.
- `tools/eth-sim/setup_interfaces.ps1` owns the Windows demo addresses/routes and requires Administrator PowerShell.
- `tools/rk-eth-uart/rk_eth_uart.py` owns COM3 access and board-side UDP send commands.
- Close the GUI before an automated live run because COM3 permits one opener.
- Live payload evidence is exact peer data or a complete board `data_hex`; offline Ethernet/IPoC fixtures are separate structural checks.
- Demo values `47000`, `10.240.1.34/24`, and `10.240.1.36` remain configurable and are not formal ICD acceptance values.
