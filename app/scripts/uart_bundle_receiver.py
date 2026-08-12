#!/usr/bin/env python3
"""Receive a Youyeetoo development bundle from a serial console."""

import argparse
import hashlib
import os
import select
import struct
import sys
import termios
import time
import zlib

MAGIC = b"C01U"
ABORT_MAGIC = b"C01X"
ACK_MAGIC = b"C01A"
HEADER = struct.Struct("<4sIII")
ACK = struct.Struct("<4sI")


def read_exact(stream, size, timeout):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out after {len(data)}/{size} bytes")
        ready, _, _ = select.select([stream], [], [], remaining)
        if not ready:
            continue
        chunk = os.read(stream.fileno(), size - len(data))
        if not chunk:
            raise EOFError("serial input closed")
        data.extend(chunk)
    return bytes(data)


def set_raw(fd):
    if not os.isatty(fd):
        return None
    previous = termios.tcgetattr(fd)
    raw = termios.tcgetattr(fd)
    raw[0] = 0
    raw[1] = 0
    raw[2] = (raw[2] & ~(termios.CSIZE | termios.PARENB)) | termios.CS8
    raw[3] = 0
    raw[6][termios.VMIN] = 1
    raw[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, raw)
    return previous


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--size", required=True, type=int)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    output = os.path.abspath(args.output)
    part = output + ".part"
    os.makedirs(os.path.dirname(output), exist_ok=True)
    previous = set_raw(sys.stdin.fileno())
    digest = hashlib.sha256()
    received = 0
    expected_seq = 0

    try:
        os.write(sys.stdout.fileno(), b"C01_READY\n")
        with open(part, "wb") as target:
            while True:
                magic, sequence, length, expected_crc = HEADER.unpack(
                    read_exact(sys.stdin.buffer, HEADER.size, args.timeout)
                )
                if magic == ABORT_MAGIC:
                    raise RuntimeError("transfer aborted by sender")
                if magic != MAGIC:
                    raise ValueError(f"bad frame magic: {magic!r}")
                if sequence not in (expected_seq, expected_seq - 1):
                    raise ValueError(f"expected sequence {expected_seq}, got {sequence}")
                if length == 0:
                    os.write(sys.stdout.fileno(), ACK.pack(ACK_MAGIC, sequence))
                    break
                payload = read_exact(sys.stdin.buffer, length, args.timeout)
                actual_crc = zlib.crc32(payload) & 0xFFFFFFFF
                if actual_crc != expected_crc:
                    raise ValueError(
                        f"CRC mismatch at sequence {sequence}: {actual_crc:08x} != {expected_crc:08x}"
                    )
                if sequence == expected_seq - 1:
                    os.write(sys.stdout.fileno(), ACK.pack(ACK_MAGIC, sequence))
                    continue
                target.write(payload)
                digest.update(payload)
                received += length
                os.write(sys.stdout.fileno(), ACK.pack(ACK_MAGIC, sequence))
                expected_seq += 1
            target.flush()
            os.fsync(target.fileno())

        actual_sha = digest.hexdigest()
        if received != args.size:
            raise ValueError(f"size mismatch: {received} != {args.size}")
        if actual_sha.lower() != args.sha256.lower():
            raise ValueError(f"SHA-256 mismatch: {actual_sha} != {args.sha256}")
        os.replace(part, output)
    except Exception as exc:
        try:
            os.unlink(part)
        except FileNotFoundError:
            pass
        if previous is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, previous)
            previous = None
        print(f"UART_RECEIVE_FAIL {exc}", file=sys.stderr, flush=True)
        return 1
    finally:
        if previous is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, previous)

    print(f"UART_RECEIVE_OK path={output} size={received} sha256={actual_sha}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
