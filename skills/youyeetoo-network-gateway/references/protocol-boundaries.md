# Network Protocol Boundaries

## Formal Or Documented Behavior

- Multi-byte application fields use big-endian encoding.
- Command, parameter, and file packets use identifier `0xEB90`; response and telemetry packets use `0x1ACF`.
- The data-length field equals packet-data bytes minus one and excludes the trailing checksum.
- The checksum starts after the identifier, covers the data field, uses byte summation followed by bitwise inversion, and keeps the low 16 bits.
- File segmentation uses explicit segment state and segment numbers, with retransmission behavior required by the project documents.
- Priority order is software uplink, user traffic, then management and logs.

## Demo Assumptions - Not Frozen ICD

- Payload-side platform address `10.240.1.34/24` was selected because the formal value was missing.
- UDP port `47000` was selected for the demo because a formal application port was missing.
- Current code does not enforce document MAC addressing and relies on standard ARP.
- The current board driver rejected `tc prio`; the demo can classify priority but cannot prove hardware or driver scheduling.

## Must Remain Configurable

- Interface-to-role mapping
- Static IPs and subnet masks
- TCP/UDP/SCTP selection and ports
- MTU, rate limits, QoS, heartbeat, timeouts, and failover
- Packet framing, ACK/retry, resume semantics, and maximum transfer unit

Never promote a demo assumption into production code or a test acceptance criterion without explicit ICD approval.
