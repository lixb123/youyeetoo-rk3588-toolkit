import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))

from youyeetoo_protocol import command, decode, file_packet


class ProtocolTests(unittest.TestCase):
    def test_command_round_trip(self):
        packet = command(0x0101, params=b"\x01\x02", sequence=7)
        decoded = decode(packet)
        self.assertEqual(decoded["opcode"], 0x0101)
        self.assertEqual(decoded["sequence"], 7)
        self.assertEqual(decoded["data"], b"\x01\x01\x01\x02")

    def test_file_round_trip(self):
        packet = file_packet(3, b"abc", grouping=1, sequence=3)
        decoded = decode(packet)
        self.assertEqual(decoded["file_block"], 3)
        self.assertEqual(decoded["file_data"], b"abc")

    def test_checksum_is_checked(self):
        packet = bytearray(command(0x001D))
        packet[-1] ^= 0x01
        with self.assertRaises(ValueError):
            decode(bytes(packet))


if __name__ == "__main__":
    unittest.main()
