# Network Test Workflow

## Read-Only Baseline

Run `scripts/collect-network-facts.sh` before any configuration change. Confirm which interface carries SSH and whether UART is available.

## Smoke Test Levels

1. Interface: presence, driver, carrier, speed, duplex, counters.
2. Local socket: bind, send, receive, and self-test.
3. Direct cable: ARP, ping, TCP connection, and bidirectional UDP.
4. Protocol: valid frame, invalid identifier, bad length, bad checksum, segmentation, duplicates, and retransmission.
5. Performance: sustained throughput, loss, CPU, memory, NVMe interaction, and concurrent links.
6. Reliability: cable removal, peer restart, process restart, route recovery, watchdog interaction, and long duration.

## Existing Demo Commands

Board-local smoke test:

```bash
./youyeetoo_eth_demo --iface eth1 --self-test --count 20 --payload-size 4096
```

Listener example:

```bash
./youyeetoo_eth_demo --iface eth1 --listen --bind 192.168.137.2 --port 47000 --count 20
```

Gateway demo:

```bash
make
sudo ./configure_gateway.sh eth0 eth1
sudo ./youyeetoo_eth_gateway --payload-iface eth0 --x-iface eth1
```

The configuration script changes addresses and forwarding state. Use it only after confirming interface roles and preserving management access.

## Acceptance Evidence

Record interface identity, peer identity, exact addresses, cable topology, commands, duration, packet counts, bytes, loss, errors, throughput, CPU, logs, and whether each value is a formal requirement or demo configuration.
