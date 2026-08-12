import unittest

from rk_eth_uart import CHANNELS, checksum, command_packet, file_packet


class ProtocolTest(unittest.TestCase):
    def test_command(self):
        value = command_packet("001D", "", "0")
        self.assertEqual(value.hex().upper(), "EB9003D0C0000001001DFE4E")
        self.assertEqual(int.from_bytes(value[-2:], "big"), checksum(value[:-2]))

    def test_file(self):
        value = file_packet("7", "01 02", "3", "9")
        self.assertEqual(value[:2], b"\xEB\x90")
        self.assertEqual(value[2:4], b"\x03\xDF")
        self.assertEqual(value[8:12], b"\x00\x07\x01\x02")
        self.assertEqual(int.from_bytes(value[-2:], "big"), checksum(value[:-2]))

    def test_default_board_reply_direction(self):
        source, target, iface, priority = CHANNELS[next(iter(CHANNELS))]
        self.assertEqual((source, target, iface, priority),
                         ("10.240.1.36", "10.240.1.2", "eth1", 0))


if __name__ == "__main__":
    unittest.main()
