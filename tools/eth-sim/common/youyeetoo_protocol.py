"""Exact application packet helpers for the Ethernet protocol document."""

from __future__ import annotations

import struct

COMMAND_MARKER = 0xEB90
RESPONSE_MARKER = 0x1ACF
APID_DEVICE = 0x3D0
APID_FILE = 0x3DF
PROTO_PORT = 47000


def checksum(packet_without_checksum: bytes) -> int:
    """Complemented byte sum of primary header excluding the marker + data."""
    if len(packet_without_checksum) < 8:
        raise ValueError("primary header is incomplete")
    total = sum(packet_without_checksum[2:])
    return (~total) & 0xFFFF


def encode_packet(*, marker: int, apid: int, grouping: int, sequence: int,
                  data: bytes, packet_type: int = 0, secondary: int = 0) -> bytes:
    if marker not in (COMMAND_MARKER, RESPONSE_MARKER):
        raise ValueError("marker must be 0xEB90 or 0x1ACF")
    if not (0 <= apid <= 0x7FF and 0 <= grouping <= 3 and 0 <= sequence <= 0x3FFF):
        raise ValueError("primary header field out of range")
    if not data or len(data) > 0x10000:
        raise ValueError("data field must contain 1..65536 bytes")
    word1 = ((0 & 0x7) << 13) | ((packet_type & 1) << 12) | ((secondary & 1) << 11) | apid
    word2 = ((grouping & 3) << 14) | sequence
    primary = struct.pack(">HHHH", marker, word1, word2, len(data) - 1)
    value = primary + data
    return value + struct.pack(">H", checksum(value))


def command(opcode: int, params: bytes = b"", sequence: int = 0,
            apid: int = APID_DEVICE) -> bytes:
    """Single-frame control/parameter packet (APID low nibble 0)."""
    if apid & 0xF:
        raise ValueError("control APID low nibble must be 0")
    return encode_packet(marker=COMMAND_MARKER, apid=apid, grouping=3,
                         sequence=sequence, data=struct.pack(">H", opcode) + params)


def file_packet(block: int, payload: bytes, *, grouping: int,
                sequence: int, apid: int = APID_FILE) -> bytes:
    """File packet: 2-byte block number followed by up to 1000 bytes."""
    if apid & 0xF != 0xF:
        raise ValueError("file APID low nibble must be 0xF")
    if not 0 <= block <= 0xFFFF or len(payload) > 1000:
        raise ValueError("file block or payload out of range")
    return encode_packet(marker=COMMAND_MARKER, apid=apid, grouping=grouping,
                         sequence=sequence, data=struct.pack(">H", block) + payload)


def decode(packet: bytes) -> dict:
    if len(packet) < 10:
        raise ValueError("packet shorter than marker + primary header + checksum")
    marker, word1, word2, length_minus_one = struct.unpack(">HHHH", packet[:8])
    if marker not in (COMMAND_MARKER, RESPONSE_MARKER):
        raise ValueError(f"bad marker 0x{marker:04X}")
    data_length = length_minus_one + 1
    expected = 8 + data_length + 2
    if len(packet) != expected:
        raise ValueError(f"length mismatch: header expects {expected}, got {len(packet)}")
    received, = struct.unpack(">H", packet[-2:])
    calculated = checksum(packet[:-2])
    if received != calculated:
        raise ValueError(f"checksum mismatch: got 0x{received:04X}, expected 0x{calculated:04X}")
    data = packet[8:-2]
    result = {
        "marker": marker,
        "version": (word1 >> 13) & 0x7,
        "type": (word1 >> 12) & 1,
        "secondary": (word1 >> 11) & 1,
        "apid": word1 & 0x7FF,
        "grouping": (word2 >> 14) & 3,
        "sequence": word2 & 0x3FFF,
        "data_length": data_length,
        "checksum": received,
        "data": data,
    }
    if result["apid"] & 0xF == 0xF and len(data) >= 2:
        result["file_block"] = struct.unpack(">H", data[:2])[0]
        result["file_data"] = data[2:]
    elif len(data) >= 2:
        result["opcode"] = struct.unpack(">H", data[:2])[0]
    return result


def hex_packet(packet: bytes) -> str:
    return packet.hex(" ").upper()
