---
name: youyeetoo-auto-ethernet-validation
description: Execute and report Youyeetoo RK3588 Ethernet closed-loop validation through Windows dual NICs and the existing COM3 UART control path. Use for automated NB-IoT payload and X telemetry/control simulation, seeded protocol campaigns, rk-eth-uart and eth-sim integration, board gateway data_hex/priority checks, PC wire reception, IPoC/Ethernet structure checks, PASS/FAIL/BLOCKED evidence reports, and regression runs without SSH.
---

# Youyeetoo Automated Ethernet Validation

Declare `Agent D-AUTO`. Use UART for board control; do not enable or assume SSH.

## Required Inputs

1. Read `references/validation-boundary.md`.
2. Require the repository programs at `tools/eth-sim` and `tools/rk-eth-uart` (or compatible copies supplied by the operator).
3. Require the board gateway at `/home/youyeetoo/youyeetoo_eth_gateway` and COM3 shell login at `1500000 8N1`, no flow control.
4. Generate vectors through `$youyeetoo-random-protocol`; keep its seed unchanged in all reports.

## Workflow

1. Collect Windows adapter/address/route state and UART board `uname`, `ip -br addr`, routes, counters, and gateway process before testing.
2. Run offline validation first:
   `scripts/run_campaign.py --mode offline --seed <seed>`.
3. Run live validation only when both cables and UART shell are ready:
   `scripts/run_campaign.py --mode live --com COM3 --baud 1500000 --seed <same-seed>`.
4. For PC-to-board tests, send from the configured PC address and require one board `RX` line matching interface, source IP, destination IP, priority, and complete `data_hex`.
5. For board-to-PC tests, start the PC UDP listener first, command the board through UART, and compare received payload bytes exactly.
6. Do not count `PACKET_OUTGOING`, UART command echo, a local socket receive, or an offline fixture as live peer receipt.
7. Write JSONL vectors plus JSON, CSV, and Markdown reports. Preserve raw evidence and seed.

## Verdict Rules

- `PASS`: observed result exactly satisfies that check.
- `FAIL`: required observable behavior ran and differed or timed out.
- `BLOCKED`: required hardware, capability, credential, cable, or formal definition is unavailable.
- Never convert a blocked PFC, LDPC/RS, RF, or wire-FCS check into PASS using application-layer simulation.

## Completion

Report adapter topology, board facts, seed, counts, exact evidence directory, failures, blocked scope, and overall `PASS | FAIL | BLOCKED`.
