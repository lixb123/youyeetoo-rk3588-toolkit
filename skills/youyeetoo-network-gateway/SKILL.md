---
name: youyeetoo-network-gateway
description: Implement, inspect, and validate the Youyeetoo RK3588 Ethernet gateway and payload links. Use for eth0/eth1/eth2, dual-port or three-port topology, NB-IoT payload networking, X telemetry/control, X data transmission, AF_PACKET, UDP protocol port 47000, routing, ARP, tc priority, link speed, packet capture, throughput, and network fault-isolation tasks.
---

# Youyeetoo Network Gateway

## Start With Evidence

1. Read `references/current-topology.md` before assigning interface roles or addresses.
2. Read `references/protocol-boundaries.md` before treating demo values as formal ICD values.
3. Read `references/test-workflow.md` before changing interface configuration or running peer tests.
4. Run `scripts/collect-network-facts.sh` for a read-only snapshot when board shell access is available.

Declare `Agent A-NET` for implementation and `Agent D-VAL` for board-only testing.

## Separate Three Layers

- Physical/link: interface presence, carrier, negotiated speed, duplex, counters, driver, and topology.
- IP/routing: addresses, routes, ARP/neighbors, forwarding, firewall, and ownership of the management link.
- Application protocol: AF_PACKET observation, UDP framing, command parsing, checksums, retransmission, and business priority.

Do not claim application protocol completion from ping or a small UDP smoke test.

## Change Rules

- Collect the current state before changing addresses, routes, qdisc, firewall, or forwarding.
- Preserve the active management path. Do not reconfigure the SSH interface without an alternate UART or network path.
- Treat `configure_gateway.sh` as state-changing because it clears selected interface addresses.
- Keep interface names, IP addresses, ports, and protocol selection configurable until the formal ICD is frozen.
- Label every value as formal requirement, current board fact, or demo assumption.
- Do not fake PAUSE/PFC frames in an application to compensate for missing driver or switch support.

## Completion Output

Report topology, link facts, address and route state, packet path, protocol coverage, measured results, assumptions, missing ICD fields, and the next hardware-backed test.
