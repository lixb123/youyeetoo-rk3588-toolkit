#!/usr/bin/env python3
"""Generate reproducible protocol-aware Youyeetoo Ethernet test vectors."""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import time
from pathlib import Path

from protocol_codec import (
    APID_FILE, COMMAND_MARKER, RESPONSE_MARKER, decode_application,
    decode_ethernet_ipv4, decode_ipoc_core, encode_command, encode_ethernet_ipv4,
    encode_file, encode_ipoc_core, encode_ipv4_udp, mutate,
)

OPCODES = [0x001D, 0x0101, 0x01AF, 0x01C5, 0x018C, 0x01D1, 0x01DA,
           0x3403, 0x340A, 0x340E, 0x3503, 0x0001]

FLOWS = [
    {"business": "x-control-upload", "role": "x-control", "direction": "pc_to_board",
     "source_ip": "10.240.1.2", "target_ip": "10.240.1.36", "board_iface": "eth1",
     "pc_adapter": "以太网", "priority": 0, "formal_transport": "UDP/custom", "port": 47000,
     "content": "application-command", "status": "DEMO_ASSUMPTION"},
    {"business": "x-control-response", "role": "x-control", "direction": "board_to_pc",
     "source_ip": "10.240.1.36", "target_ip": "10.240.1.2", "board_iface": "eth1",
     "pc_adapter": "以太网", "priority": 0, "formal_transport": "UDP/custom", "port": 47000,
     "content": "application-response", "status": "DEMO_ASSUMPTION"},
    {"business": "x-file-first", "role": "x-control", "direction": "pc_to_board",
     "source_ip": "10.240.1.2", "target_ip": "10.240.1.36", "board_iface": "eth1",
     "pc_adapter": "以太网", "priority": 0, "formal_transport": "UDP/custom", "port": 47000,
     "content": "file-first", "status": "DEMO_ASSUMPTION"},
    {"business": "x-file-middle", "role": "x-control", "direction": "pc_to_board",
     "source_ip": "10.240.1.2", "target_ip": "10.240.1.36", "board_iface": "eth1",
     "pc_adapter": "以太网", "priority": 0, "formal_transport": "UDP/custom", "port": 47000,
     "content": "file-middle", "status": "DEMO_ASSUMPTION"},
    {"business": "x-file-last", "role": "x-control", "direction": "pc_to_board",
     "source_ip": "10.240.1.2", "target_ip": "10.240.1.36", "board_iface": "eth1",
     "pc_adapter": "以太网", "priority": 0, "formal_transport": "UDP/custom", "port": 47000,
     "content": "file-last", "status": "DEMO_ASSUMPTION"},
    {"business": "nb-n6", "role": "nb-iot", "direction": "pc_to_board",
     "source_ip": "10.240.1.38", "target_ip": "10.240.1.50", "board_iface": "eth0",
     "pc_adapter": "以太网 3", "priority": 1, "formal_transport": "IP transparent", "port": 47000,
     "content": "opaque", "status": "CONFIRMED"},
    {"business": "nb-n6-return", "role": "nb-iot", "direction": "board_to_pc",
     "source_ip": "10.240.1.50", "target_ip": "10.240.1.38", "board_iface": "eth0",
     "pc_adapter": "以太网 3", "priority": 1, "formal_transport": "IP transparent", "port": 47000,
     "content": "opaque", "status": "CONFIRMED"},
    {"business": "nb-s1", "role": "nb-iot", "direction": "pc_to_board",
     "source_ip": "10.240.1.37", "target_ip": "10.240.1.40", "board_iface": "eth0",
     "pc_adapter": "以太网 3", "priority": 1, "formal_transport": "SCTP/UDP:36412", "port": 36412,
     "content": "opaque", "status": "CONFIRMED"},
    {"business": "nb-base-management", "role": "nb-iot", "direction": "pc_to_board",
     "source_ip": "10.240.1.35", "target_ip": "10.240.1.51", "board_iface": "eth0",
     "pc_adapter": "以太网 3", "priority": 2, "formal_transport": "NETCONF/TCP:830 or SFTP", "port": 47000,
     "content": "opaque", "status": "DEMO_ASSUMPTION"},
    {"business": "nb-core-management", "role": "nb-iot", "direction": "pc_to_board",
     "source_ip": "10.240.1.39", "target_ip": "10.240.1.52", "board_iface": "eth0",
     "pc_adapter": "以太网 3", "priority": 2, "formal_transport": "HTTP/TCP or SFTP/FTP", "port": 47000,
     "content": "opaque", "status": "DEMO_ASSUMPTION"},
]


def random_bytes(rng: random.Random, minimum: int, maximum: int) -> bytes:
    return bytes(rng.randrange(256) for _ in range(rng.randint(minimum, maximum)))


def payload_for(flow: dict, rng: random.Random, sequence: int) -> tuple[bytes, dict]:
    if flow["content"] == "application-command":
        opcode = rng.choice(OPCODES)
        payload = encode_command(opcode, random_bytes(rng, 0, 48), sequence)
        return payload, {"kind": "application", "decoded": decode_application(payload)}
    if flow["content"] == "application-response":
        opcode = rng.choice(OPCODES)
        payload = encode_command(opcode, random_bytes(rng, 0, 48), sequence, RESPONSE_MARKER)
        return payload, {"kind": "application", "decoded": decode_application(payload)}
    if flow["content"].startswith("file-"):
        grouping = {"file-first": 1, "file-middle": 0, "file-last": 2}[flow["content"]]
        size = 1000 if grouping in (0, 1) else rng.randint(1, 999)
        payload = encode_file(sequence, random_bytes(rng, size, size), grouping, sequence)
        return payload, {"kind": "application", "decoded": decode_application(payload)}
    payload = random_bytes(rng, 16, 256)
    return payload, {"kind": "opaque", "decoded": None}


def vector(flow: dict, rng: random.Random, index: int, seed: int) -> dict:
    payload, protocol = payload_for(flow, rng, index & 0x3FFF)
    source_port = rng.randint(20000, 60000)
    ipv4 = encode_ipv4_udp(flow["source_ip"], flow["target_ip"], source_port,
                           flow["port"], payload, identification=rng.randrange(0x10000))
    # MAC values are deterministic test fixtures. Actual link tests use ARP-derived hardware MACs.
    ethernet = encode_ethernet_ipv4("5A:3D:01:00:00:01", "5A:3D:02:00:00:02", ipv4)
    try:
        ipoc = encode_ipoc_core(ipv4, index & 0xFFFFFF, index & 0xF)
        ipoc_hex = ipoc.hex().upper()
        ipoc_scope = "sync+AOS+M_PDU+CRC; LDPC/RS omitted"
        decode_ipoc_core(ipoc)
    except ValueError as exc:
        ipoc_hex = None
        ipoc_scope = f"requires IPoC cross-frame fragmentation/reassembly: {exc}"
    decode_ethernet_ipv4(ethernet)
    result = dict(flow)
    result.update({
        "id": f"V{index:04d}", "seed": seed, "valid": True,
        "source_port": source_port, "payload_kind": protocol["kind"],
        "payload_hex": payload.hex().upper(), "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "expected_application": protocol["decoded"],
        "ipv4_udp_hex": ipv4.hex().upper(), "ethernet_fixture_hex": ethernet.hex().upper(),
        "ipoc_core_hex": ipoc_hex, "ipoc_core_scope": ipoc_scope,
        "fixture_mac_scope": "offline deterministic fixture; live MAC is resolved by ARP",
    })
    return result


def negative_vector(rng: random.Random, index: int, seed: int) -> dict:
    flow = dict(FLOWS[0])
    opcode = rng.choice(OPCODES)
    good = encode_command(opcode, random_bytes(rng, 0, 24), index & 0x3FFF)
    mutation = rng.choice(("bad_checksum", "bad_marker", "bad_length"))
    bad = mutate(good, mutation)
    flow.update({
        "id": f"N{index:04d}", "seed": seed, "valid": False,
        "negative_intent": mutation, "payload_kind": "application",
        "payload_hex": bad.hex().upper(), "payload_sha256": hashlib.sha256(bad).hexdigest(),
        "expected_application": None, "status": "CONFIRMED",
    })
    return flow


def generate(seed: int, count: int, negative_count: int) -> list[dict]:
    rng = random.Random(seed)
    rows = [vector(FLOWS[index % len(FLOWS)], rng, index, seed) for index in range(count)]
    rows.extend(negative_vector(rng, index, seed) for index in range(negative_count))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--count", type=int, default=14)
    parser.add_argument("--negative-count", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    seed = args.seed if args.seed is not None else time.time_ns() & 0xFFFFFFFF
    if args.count < 1 or args.negative_count < 0:
        parser.error("count must be positive and negative-count cannot be negative")
    rows = generate(seed, args.count, args.negative_count)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    print(json.dumps({"seed": seed, "vectors": len(rows), "output": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
