#!/usr/bin/env python3
"""Verify BOOT PASS with binary and YMODEM-1K-sized payloads.

The first port is the ESP32 native USB CDC port. The second port is an
independent USB-to-RS485 adapter connected to the ESP32 RS485 connector.
Both directions are tested sequentially, so the script never depends on a
text delimiter or a Modbus frame gap.

Example:
    python tools/test_boot_pass.py COM7 COM5 --count 5
"""

from __future__ import annotations

import argparse
import hashlib
import random
import sys
import time

try:
    import serial
except ImportError:
    print("Missing pyserial. Install it with: python -m pip install pyserial",
          file=sys.stderr)
    raise SystemExit(2)


BAUDRATE = 9600
YMODEM_PACKET_SIZE = 1029


def crc16_xmodem(data: bytes) -> int:
    """Return the big-endian CRC value used by X/YMODEM data packets."""
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (
                crc << 1) & 0xFFFF
    return crc


def make_packet(sequence: int, data: bytes) -> bytes:
    """Build one exact 1029-byte YMODEM-1K STX packet."""
    if len(data) != 1024:
        raise ValueError("YMODEM-1K data must contain exactly 1024 bytes")
    sequence &= 0xFF
    crc = crc16_xmodem(data)
    return bytes((0x02, sequence, 0xFF - sequence)) + data + crc.to_bytes(2, "big")


def open_port(name: str) -> serial.Serial:
    """Open a port as 9600-8-N-1 without toggling ESP32 reset lines."""
    port = serial.Serial()
    port.port = name
    port.baudrate = BAUDRATE
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_ONE
    port.timeout = 0.1
    port.write_timeout = 5
    port.dsrdtr = False
    port.rtscts = False
    port.dtr = False
    port.rts = False
    port.open()
    return port


def read_exact(port: serial.Serial, length: int, timeout: float) -> bytes:
    """Collect exactly length bytes with one overall deadline."""
    received = bytearray()
    deadline = time.monotonic() + timeout
    while len(received) < length and time.monotonic() < deadline:
        chunk = port.read(length - len(received))
        if chunk:
            received.extend(chunk)
    return bytes(received)


def first_difference(expected: bytes, actual: bytes) -> str:
    """Describe the first mismatch without dumping a binary packet."""
    shared = min(len(expected), len(actual))
    for offset in range(shared):
        if expected[offset] != actual[offset]:
            return (f"offset {offset}: expected 0x{expected[offset]:02X}, "
                    f"received 0x{actual[offset]:02X}; "
                    f"length expected={len(expected)} received={len(actual)}")
    return f"length: expected {len(expected)}, received {len(actual)}"


def transfer(source: serial.Serial, destination: serial.Serial,
             payload: bytes, timeout: float) -> None:
    """Send one uninterrupted burst and require a byte-identical result."""
    source.reset_input_buffer()
    destination.reset_input_buffer()
    written = source.write(payload)
    source.flush()
    if written != len(payload):
        raise RuntimeError(f"short PC write: {written}/{len(payload)}")
    received = read_exact(destination, len(payload), timeout)
    if received != payload:
        expected_hash = hashlib.sha256(payload).hexdigest()[:16]
        actual_hash = hashlib.sha256(received).hexdigest()[:16]
        raise RuntimeError(
            f"{first_difference(payload, received)}; "
            f"SHA256 expected={expected_hash} received={actual_hash}")


def payloads() -> list[tuple[str, bytes]]:
    """Cover control bytes, all-zero/all-one data, and deterministic noise."""
    rng = random.Random(0x50595849)
    patterns = [
        ("incrementing", bytes(value & 0xFF for value in range(1024))),
        ("all-zero", bytes(1024)),
        ("all-ff", bytes((0xFF,)) * 1024),
        ("random", bytes(rng.randrange(256) for _ in range(1024))),
    ]
    return [(name, make_packet(index + 1, data))
            for index, (name, data) in enumerate(patterns)]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Test ESP32 BOOT PASS in both directions")
    parser.add_argument("esp_port", help="ESP32 native USB CDC port, e.g. COM7")
    parser.add_argument("rs485_port", help="USB-to-RS485 adapter, e.g. COM5")
    parser.add_argument("--count", type=int, default=5,
                        help="repetitions per pattern and direction (default: 5)")
    parser.add_argument("--timeout", type=float, default=4.0,
                        help="seconds allowed for each 1029-byte transfer")
    args = parser.parse_args()
    if args.count < 1 or args.timeout <= 0:
        parser.error("--count and --timeout must be positive")

    print(f"Open ESP={args.esp_port}, RS485={args.rs485_port} at 9600-8-N-1")
    with open_port(args.esp_port) as esp, open_port(args.rs485_port) as adapter:
        time.sleep(0.2)
        esp.reset_input_buffer()
        adapter.reset_input_buffer()
        cases = payloads()
        completed = 0
        total = len(cases) * args.count * 2
        for repeat in range(1, args.count + 1):
            for name, packet in cases:
                try:
                    transfer(esp, adapter, packet, args.timeout)
                    completed += 1
                    print(f"[{completed:03d}/{total:03d}] ESP->RS485 "
                          f"{name} repeat={repeat} PASS")
                    transfer(adapter, esp, packet, args.timeout)
                    completed += 1
                    print(f"[{completed:03d}/{total:03d}] RS485->ESP "
                          f"{name} repeat={repeat} PASS")
                except Exception as error:
                    print(f"RESULT: FAIL - {error}", file=sys.stderr)
                    return 1

    bytes_per_direction = len(payloads()) * args.count * YMODEM_PACKET_SIZE
    print(f"Bytes per direction: {bytes_per_direction}")
    print("Check BOOT PASS screen: DROP must remain 0/0")
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
