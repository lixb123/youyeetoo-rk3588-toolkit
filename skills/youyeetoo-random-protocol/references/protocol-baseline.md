# Protocol Baseline

## Confirmed application rules

- Command/parameter/file marker: `0xEB90`; response/telemetry marker: `0x1ACF`.
- Multi-byte fields are big-endian.
- Data length equals data-field bytes minus one and excludes the checksum.
- Checksum covers bytes after the marker through the data field; sum bytes, invert, keep low 16 bits.
- Command APID low nibble is `0`; file APID low nibble is `0xF`.
- File grouping: first `1`, middle `0`, last `2`, unsegmented `3`; first/middle data is 1000 bytes.
- Priority: platform/upload `0`; N6/S1 `1`; management/log `2`; other `3`.

## Confirmed IPoC rules

- Sync `1ACFFC1D`; spacecraft identifier `63`; IPoC VCID `101000b`.
- AOS header 6 bytes; M_PDU data field 884 bytes; frame-error-control 2 bytes.
- ENCAP fields: version `111b`, protocol `010b`, length type `10b`, 4-bit sequence, 16-bit total length.
- IPE `0x0800` for IPv4; unused M_PDU bytes are `0xAA`.
- CRC polynomial `x16+x12+x5+1`, initial state all ones, over AOS header and transfer data.
- X telemetry/control link: platform `10.240.1.1/30`, X machine `10.240.1.2/30`, standard ARP.

## Demo assumptions

- UDP port `47000`.
- Payload-side platform address `10.240.1.34/24`.
- Platform-interaction source/address `10.240.1.36` in the current demo.
- Offline MAC fixtures are deterministic samples, not spacecraft-assigned production MACs.

## Hardware-only or missing

- The document reserves 128 bytes for LDPC/RS but does not provide a complete implementation baseline here.
- RF randomization and coding need the X-machine/radio implementation and capture evidence.
- PFC needs IEEE 802.1Qbb-capable hardware, priority queues, and counters. Application packets cannot substitute.
- Normal NIC capture commonly strips FCS; claim wire FCS only with a capable tap/NIC.
