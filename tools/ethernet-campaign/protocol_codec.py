#!/usr/bin/env python3
"""Deterministic Youyeetoo application, Ethernet II, and IPoC codecs."""

from __future__ import annotations

import binascii
import ipaddress
import struct
from dataclasses import dataclass

COMMAND_MARKER = 0xEB90
RESPONSE_MARKER = 0x1ACF
APID_DEVICE = 0x3D0
APID_FILE = 0x3DF
IPOC_SYNC = bytes.fromhex("1ACFFC1D")
IPOC_SPACECRAFT_ID = 63
IPOC_VCID = 0b101000
IPOC_DATA_SIZE = 884
IPOC_M_PDU_PAYLOAD_SIZE = 882
IPOC_ENCAP_FIRST = 0b111_010_10
ETHERTYPE_IPV4 = 0x0800


def complement_sum(packet_without_checksum: bytes) -> int:
    if len(packet_without_checksum) < 8:
        raise ValueError("application header is incomplete")
    return (~sum(packet_without_checksum[2:])) & 0xFFFF


def encode_application(*, marker: int, apid: int, grouping: int,
                       sequence: int, data: bytes) -> bytes:
    if marker not in (COMMAND_MARKER, RESPONSE_MARKER):
        raise ValueError("marker must be EB90 or 1ACF")
    if not (0 <= apid <= 0x7FF and 0 <= grouping <= 3 and 0 <= sequence <= 0x3FFF):
        raise ValueError("application header field out of range")
    if not 1 <= len(data) <= 0x10000:
        raise ValueError("application data must contain 1..65536 bytes")
    header = struct.pack(">HHHH", marker, apid, grouping << 14 | sequence, len(data) - 1)
    packet = header + data
    return packet + struct.pack(">H", complement_sum(packet))


def encode_command(opcode: int, params: bytes, sequence: int,
                   marker: int = COMMAND_MARKER) -> bytes:
    if len(params) > 254:
        raise ValueError("opcode plus parameters exceeds the documented 256-byte limit")
    return encode_application(marker=marker, apid=APID_DEVICE, grouping=3,
                              sequence=sequence, data=struct.pack(">H", opcode) + params)


def encode_file(block: int, payload: bytes, grouping: int, sequence: int) -> bytes:
    if not 0 <= block <= 0xFFFF or len(payload) > 1000:
        raise ValueError("file block or payload is out of range")
    if grouping in (0, 1) and len(payload) != 1000:
        raise ValueError("first and middle file segments must contain 1000 data bytes")
    return encode_application(marker=COMMAND_MARKER, apid=APID_FILE,
                              grouping=grouping, sequence=sequence,
                              data=struct.pack(">H", block) + payload)


def decode_application(packet: bytes) -> dict:
    if len(packet) < 12:
        raise ValueError("application packet is too short")
    marker, word1, word2, length_minus_one = struct.unpack(">HHHH", packet[:8])
    if marker not in (COMMAND_MARKER, RESPONSE_MARKER):
        raise ValueError(f"bad marker 0x{marker:04X}")
    expected = 8 + length_minus_one + 1 + 2
    if len(packet) != expected:
        raise ValueError(f"length mismatch: expected {expected}, got {len(packet)}")
    received = struct.unpack(">H", packet[-2:])[0]
    calculated = complement_sum(packet[:-2])
    if received != calculated:
        raise ValueError(f"checksum mismatch: got 0x{received:04X}, expected 0x{calculated:04X}")
    data = packet[8:-2]
    result = {
        "marker": marker,
        "version": word1 >> 13 & 7,
        "type": word1 >> 12 & 1,
        "secondary": word1 >> 11 & 1,
        "apid": word1 & 0x7FF,
        "grouping": word2 >> 14,
        "sequence": word2 & 0x3FFF,
        "data_length": len(data),
        "checksum": received,
        "data_hex": data.hex().upper(),
    }
    if result["version"] or result["type"] or result["secondary"]:
        raise ValueError("reserved application primary-header bits are not zero")
    if result["apid"] & 0xF == 0xF:
        if len(data) < 2:
            raise ValueError("file packet has no segment number")
        result["file_block"] = struct.unpack(">H", data[:2])[0]
        result["file_data_length"] = len(data) - 2
        if len(data) - 2 > 1000:
            raise ValueError("file segment exceeds 1000 bytes")
    else:
        if result["apid"] & 0xF:
            raise ValueError("command APID low nibble is not zero")
        if len(data) < 2:
            raise ValueError("command/response has no opcode")
        result["opcode"] = struct.unpack(">H", data[:2])[0]
        if marker == COMMAND_MARKER and len(data) > 256:
            raise ValueError("command data exceeds 256 bytes")
    return result


def internet_checksum(value: bytes) -> int:
    if len(value) & 1:
        value += b"\0"
    total = sum(struct.unpack(f">{len(value) // 2}H", value))
    total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def encode_ipv4_udp(source_ip: str, target_ip: str, source_port: int,
                    target_port: int, payload: bytes, identification: int = 0) -> bytes:
    source = ipaddress.IPv4Address(source_ip).packed
    target = ipaddress.IPv4Address(target_ip).packed
    udp_length = 8 + len(payload)
    pseudo = source + target + struct.pack(">BBH", 0, 17, udp_length)
    udp = struct.pack(">HHHH", source_port, target_port, udp_length, 0) + payload
    udp_checksum = internet_checksum(pseudo + udp) or 0xFFFF
    udp = struct.pack(">HHHH", source_port, target_port, udp_length, udp_checksum) + payload
    total_length = 20 + len(udp)
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, total_length, identification & 0xFFFF,
                     0x4000, 64, 17, 0, source, target)
    ip = ip[:10] + struct.pack(">H", internet_checksum(ip)) + ip[12:]
    return ip + udp


def decode_ipv4_udp(packet: bytes) -> dict:
    if len(packet) < 28 or packet[0] >> 4 != 4 or packet[0] & 0xF != 5:
        raise ValueError("only an unoptioned IPv4/UDP packet is accepted")
    total_length = struct.unpack(">H", packet[2:4])[0]
    if total_length != len(packet) or packet[9] != 17:
        raise ValueError("IPv4 length/protocol mismatch")
    if internet_checksum(packet[:20]) != 0:
        raise ValueError("bad IPv4 header checksum")
    source, target = packet[12:16], packet[16:20]
    source_port, target_port, udp_length, received = struct.unpack(">HHHH", packet[20:28])
    if udp_length != len(packet) - 20:
        raise ValueError("bad UDP length")
    pseudo = source + target + struct.pack(">BBH", 0, 17, udp_length)
    if received and internet_checksum(pseudo + packet[20:]) != 0:
        raise ValueError("bad UDP checksum")
    return {
        "source_ip": str(ipaddress.IPv4Address(source)),
        "target_ip": str(ipaddress.IPv4Address(target)),
        "source_port": source_port,
        "target_port": target_port,
        "payload_hex": packet[28:].hex().upper(),
    }


def mac_bytes(value: str) -> bytes:
    raw = bytes.fromhex(value.replace(":", "").replace("-", ""))
    if len(raw) != 6:
        raise ValueError("MAC address must contain six bytes")
    return raw


def encode_ethernet_ipv4(source_mac: str, target_mac: str, ipv4: bytes,
                         include_fcs: bool = True) -> bytes:
    frame = mac_bytes(target_mac) + mac_bytes(source_mac) + struct.pack(">H", ETHERTYPE_IPV4) + ipv4
    if len(frame) < 60:
        frame += bytes(60 - len(frame))
    if include_fcs:
        frame += struct.pack("<I", binascii.crc32(frame) & 0xFFFFFFFF)
    return frame


def decode_ethernet_ipv4(frame: bytes, include_fcs: bool = True) -> dict:
    if len(frame) < 60 + (4 if include_fcs else 0):
        raise ValueError("Ethernet II frame is shorter than the minimum")
    body = frame[:-4] if include_fcs else frame
    if include_fcs and struct.unpack("<I", frame[-4:])[0] != binascii.crc32(body) & 0xFFFFFFFF:
        raise ValueError("bad Ethernet FCS")
    ethertype = struct.unpack(">H", body[12:14])[0]
    if ethertype != ETHERTYPE_IPV4:
        raise ValueError(f"unexpected EtherType 0x{ethertype:04X}")
    total = struct.unpack(">H", body[16:18])[0]
    ipv4 = body[14:14 + total]
    return {
        "target_mac": body[:6].hex(":").upper(),
        "source_mac": body[6:12].hex(":").upper(),
        "ethertype": ethertype,
        "ipv4": decode_ipv4_udp(ipv4),
        "frame_length": len(frame),
    }


def crc16_ccitt(value: bytes) -> int:
    crc = 0xFFFF
    for byte in value:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else crc << 1 & 0xFFFF
    return crc


def encode_aos_header(frame_count: int, spacecraft_id: int = IPOC_SPACECRAFT_ID,
                      vcid: int = IPOC_VCID) -> bytes:
    if not (0 <= frame_count <= 0xFFFFFF and 0 <= spacecraft_id <= 0xFF and 0 <= vcid <= 0x3F):
        raise ValueError("AOS primary-header field out of range")
    word = (1 << 46) | (spacecraft_id << 38) | (vcid << 32) | (frame_count << 8)
    return word.to_bytes(6, "big")


def encode_encap(ipe_payload: bytes, sequence: int) -> bytes:
    if not 0 <= sequence <= 0xF:
        raise ValueError("ENCAP sequence must be 0..15")
    total_length = 4 + 2 + len(ipe_payload)
    if total_length > 0xFFFF:
        raise ValueError("ENCAP packet is too large")
    return bytes((IPOC_ENCAP_FIRST, sequence << 4)) + struct.pack(">H", total_length) + struct.pack(">H", ETHERTYPE_IPV4) + ipe_payload


def encode_ipoc_core(ipv4: bytes, frame_count: int, encap_sequence: int) -> bytes:
    encap = encode_encap(ipv4, encap_sequence)
    if len(encap) > IPOC_M_PDU_PAYLOAD_SIZE:
        raise ValueError("single-frame helper cannot encode a cross-frame ENCAP packet")
    mpdu = struct.pack(">H", 0) + encap + bytes((0xAA,)) * (IPOC_M_PDU_PAYLOAD_SIZE - len(encap))
    aos = encode_aos_header(frame_count) + mpdu
    return IPOC_SYNC + aos + struct.pack(">H", crc16_ccitt(aos))


def decode_ipoc_core(frame: bytes) -> dict:
    expected = 4 + 6 + IPOC_DATA_SIZE + 2
    if len(frame) != expected or frame[:4] != IPOC_SYNC:
        raise ValueError("IPoC core length or sync marker mismatch")
    aos, received_crc = frame[4:-2], struct.unpack(">H", frame[-2:])[0]
    if crc16_ccitt(aos) != received_crc:
        raise ValueError("bad IPoC frame-error-control CRC")
    header = int.from_bytes(aos[:6], "big")
    version, spacecraft, vcid = header >> 46, header >> 38 & 0xFF, header >> 32 & 0x3F
    frame_count = header >> 8 & 0xFFFFFF
    if (version, spacecraft, vcid, header & 0xFF) != (1, IPOC_SPACECRAFT_ID, IPOC_VCID, 0):
        raise ValueError("AOS fixed field mismatch")
    pointer = struct.unpack(">H", aos[6:8])[0]
    if pointer != 0:
        raise ValueError("single-packet M_PDU first-header pointer is not zero")
    encap = aos[8:]
    if encap[0] != IPOC_ENCAP_FIRST or encap[1] & 0xF:
        raise ValueError("ENCAP fixed field mismatch")
    encap_length = struct.unpack(">H", encap[2:4])[0]
    if not 6 <= encap_length <= len(encap):
        raise ValueError("ENCAP length mismatch")
    if struct.unpack(">H", encap[4:6])[0] != ETHERTYPE_IPV4:
        raise ValueError("IPE is not IPv4")
    if any(value != 0xAA for value in encap[encap_length:]):
        raise ValueError("M_PDU idle padding is not 0xAA")
    ipv4 = encap[6:encap_length]
    return {
        "spacecraft_id": spacecraft,
        "vcid": vcid,
        "frame_count": frame_count,
        "encap_sequence": encap[1] >> 4,
        "encap_length": encap_length,
        "ipe": ETHERTYPE_IPV4,
        "ipv4": decode_ipv4_udp(ipv4),
        "crc": received_crc,
    }


def mutate(packet: bytes, mutation: str) -> bytes:
    value = bytearray(packet)
    if mutation == "bad_checksum":
        value[-1] ^= 1
    elif mutation == "bad_marker":
        value[0] ^= 1
    elif mutation == "bad_length":
        value[7] ^= 1
    elif mutation == "bad_grouping":
        value[4] = value[4]  # keep APID intact
        value[4 + 2] ^= 0x40
    else:
        raise ValueError(f"unknown mutation {mutation}")
    return bytes(value)
