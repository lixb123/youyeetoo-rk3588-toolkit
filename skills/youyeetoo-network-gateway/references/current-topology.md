# Current Network Topology

## Project Intent

The target payload processor requires three independent Ethernet roles:

- X data transmission for high-rate camera and payload downlink.
- X telemetry/control for high-rate uplink and OTA reception.
- NB-IoT payload networking for local service traffic.

The final hardware, interface names, and formal ICD values are not fully closed.

## Last Verified Development Board Facts

- Board: YY3588 / RK3588, Ubuntu 22.04.5, Linux 6.1.75 AArch64.
- Only `eth0` and `eth1` were observed; no `eth2` was present.
- `eth1=192.168.137.2/24` was the SSH management link to Windows `192.168.137.1` and negotiated 1000 Mb/s full duplex.
- `eth0=169.254.11.1/16` was tested over a direct cable to a Windows ASIX adapter at `169.254.235.229/16`, also at 1000 Mb/s full duplex.
- Small bidirectional UDP tests passed 5/5 packets of 1400 bytes.

These facts prove two-link basic connectivity only. They do not prove three-port hardware, 800/900 Mb/s payload throughput, low loss, long stability, or formal protocol behavior.

## Current Code

- Smoke test: `<workspace>\rk3588_eth_demo`
- Dual-port gateway demo: `<workspace>\youyeetoo_eth_gateway`
- Gateway implementation uses AF_PACKET to observe two interfaces while the kernel performs normal ARP and IPv4 forwarding.
- The demo decodes application data on UDP port `47000` and logs next to the executable.
