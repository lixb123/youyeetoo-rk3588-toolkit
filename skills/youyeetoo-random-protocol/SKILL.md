---
name: youyeetoo-random-protocol
description: Generate reproducible valid and intentionally invalid Youyeetoo protocol vectors for NB-IoT payload, X telemetry/control, Ethernet II, IPv4/UDP, application frames 0xEB90/0x1ACF, file segments, and IP over CCSDS (IPoC). Use for seeded random data generation, protocol fuzzing, boundary cases, checksum/length/grouping tests, Ethernet/IPoC fixture validation, or preparing inputs for the RK3588 Ethernet/UART closed-loop test.
---

# Youyeetoo Random Protocol

Declare `Agent A-RND`. Keep generation deterministic and analysis-first.

## Workflow

1. Read `references/protocol-baseline.md` before generating vectors.
2. Run `scripts/generate_vectors.py --seed <integer> --count <n> --negative-count <n> --output <file.jsonl>`.
3. Preserve the seed, complete hex bytes, decoded expected fields, business route, source/target IP, expected priority, and SHA-256 for every vector.
4. Decode every valid encoded vector before emitting it. Reject generator output that does not round-trip.
5. Mark invalid vectors with `valid=false` and a single explicit `negative_intent`. Never mix malformed data into the valid corpus.
6. Label every configuration value `CONFIRMED`, `DEMO_ASSUMPTION`, `MISSING`, `CONFLICT`, or `STALE`.

## Integrity Rules

- Treat `0xEB90/0x1ACF`, big-endian fields, APID rules, length-minus-one, complemented byte sum, and file grouping as application-layer rules.
- Treat Ethernet II/FCS and IPoC fixtures as offline structural evidence unless a hardware capture proves the actual wire bytes.
- Do not claim LDPC/RS, RF randomization, PAUSE, or PFC coverage from software fixtures.
- Do not invent formal MAC addresses, UDP port, platform payload address, retry timing, or queue behavior.

## Completion

Report the seed, vector counts, valid/negative split, covered flows, round-trip result, output path, and blocked hardware-only checks.
