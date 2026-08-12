from __future__ import annotations

import unittest

from protocol_codec import (
    RESPONSE_MARKER, decode_application, decode_ethernet_ipv4, decode_ipoc_core,
    encode_command, encode_ethernet_ipv4, encode_file, encode_ipoc_core,
    encode_ipv4_udp, mutate,
)


class CodecTests(unittest.TestCase):
    def test_application_round_trip(self):
        raw = encode_command(0x001D, bytes.fromhex("010203"), 0x123, RESPONSE_MARKER)
        decoded = decode_application(raw)
        self.assertEqual(decoded["opcode"], 0x001D)
        self.assertEqual(decoded["sequence"], 0x123)

    def test_file_boundaries(self):
        raw = encode_file(0, bytes(1000), 1, 0)
        decoded = decode_application(raw)
        self.assertEqual(decoded["file_data_length"], 1000)
        with self.assertRaises(ValueError):
            encode_file(0, bytes(999), 1, 0)
        middle = decode_application(encode_file(1, bytes(1000), 0, 1))
        last = decode_application(encode_file(2, bytes(17), 2, 2))
        self.assertEqual((middle["grouping"], last["grouping"]), (0, 2))

    def test_bad_application_vectors_rejected(self):
        good = encode_command(0x001D, b"", 0)
        for kind in ("bad_marker", "bad_length", "bad_checksum"):
            with self.assertRaises(ValueError, msg=kind):
                decode_application(mutate(good, kind))

    def test_ethernet_and_ipoc_round_trip(self):
        payload = encode_command(0x3503, b"abc", 7)
        ipv4 = encode_ipv4_udp("10.240.1.2", "10.240.1.36", 30000, 47000, payload, 9)
        ethernet = encode_ethernet_ipv4("5A:01:01:00:AA:AA", "5A:02:02:00:AA:AA", ipv4)
        self.assertEqual(decode_ethernet_ipv4(ethernet)["ipv4"]["payload_hex"], payload.hex().upper())
        ipoc = encode_ipoc_core(ipv4, 0x123456, 9)
        decoded = decode_ipoc_core(ipoc)
        self.assertEqual(decoded["spacecraft_id"], 63)
        self.assertEqual(decoded["vcid"], 0b101000)
        self.assertEqual(decoded["ipv4"]["payload_hex"], payload.hex().upper())


if __name__ == "__main__":
    unittest.main()
