"""ESPCFG1 configuration upload robustness tests.

The default ``negative`` suite only sends requests that must be rejected, so
the currently installed configuration is not replaced.

Examples:
    python tools/test_espcfg1.py COM12 config/sensor_profiles.json
    python tools/test_espcfg1.py COM12 config/sensor_profiles.json --suite retry
    python tools/test_espcfg1.py COM12 config/sensor_profiles.json --suite repeat --count 5
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
except ImportError:
    print("Missing pyserial. Run: python -m pip install pyserial",
          file=sys.stderr)
    raise SystemExit(2)


MAGIC = b"ESPCFG1"
MAX_JSON_SIZE = 64 * 1024
USB_CHUNK_SIZE = 64


@dataclass(frozen=True)
class Request:
    name: str
    payload: bytes = b""
    magic: bytes = MAGIC
    declared_length: int | None = None
    declared_crc32: int | None = None


def open_port(port: str) -> serial.Serial:
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 0.2
    connection.write_timeout = 10
    connection.dsrdtr = False
    connection.rtscts = False
    connection.dtr = False
    connection.rts = False
    connection.open()
    time.sleep(0.1)
    connection.reset_input_buffer()
    return connection


def read_nak(connection: serial.Serial) -> str:
    remainder = connection.readline().decode(
        "utf-8", errors="replace").strip()
    return f"N{remainder}"


def wait_ready(connection: serial.Serial,
               timeout_seconds: float = 3.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = connection.read(1)
        if value == b"R":
            return "R"
        if value == b"N":
            return read_nak(connection)
    raise TimeoutError("timeout waiting for device Ready/NAK")


def wait_payload_ack(connection: serial.Serial,
                     timeout_seconds: float = 3.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = connection.read(1)
        if value == b"P":
            return "P"
        if value == b"N":
            return read_nak(connection)
    raise TimeoutError("timeout waiting for payload ACK/NAK")


def wait_terminal(connection: serial.Serial,
                  timeout_seconds: float = 15.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        line = connection.readline()
        if not line:
            continue
        decoded = line.decode("utf-8", errors="replace").strip()
        if decoded == "ACK" or decoded.startswith("NAK:"):
            return decoded
        if decoded:
            print(f"    diagnostic: {decoded}")
    raise TimeoutError("timeout waiting for final ACK/NAK")


def send_request(port: str, request: Request) -> str:
    length = (len(request.payload) if request.declared_length is None
              else request.declared_length)
    crc32 = (zlib.crc32(request.payload) & 0xFFFFFFFF
             if request.declared_crc32 is None
             else request.declared_crc32)
    header = request.magic + struct.pack("<II", length, crc32)

    connection = open_port(port)
    try:
        connection.write(header)
        connection.flush()
        ready = wait_ready(connection)
        if ready != "R":
            return ready

        if length != len(request.payload):
            raise RuntimeError(
                "device accepted a header whose test payload is intentionally "
                "not available")

        for offset in range(0, len(request.payload), USB_CHUNK_SIZE):
            chunk = request.payload[offset:offset + USB_CHUNK_SIZE]
            connection.write(chunk)
            connection.flush()
            payload_ack = wait_payload_ack(connection)
            if payload_ack != "P":
                return payload_ack
        return wait_terminal(connection)
    finally:
        connection.close()


def expect(port: str, request: Request, expected: str,
           prefix: bool = False) -> bool:
    print(f"[TEST] {request.name}")
    try:
        response = send_request(port, request)
    except (OSError, RuntimeError, TimeoutError,
            serial.SerialException) as error:
        print(f"  FAIL: {error}")
        return False

    matched = response.startswith(expected) if prefix else response == expected
    print(f"  {'PASS' if matched else 'FAIL'}: {response}")
    if not matched:
        print(f"    expected {'prefix ' if prefix else ''}{expected}")
    time.sleep(0.15)
    return matched


def load_valid_payload(path: Path) -> bytes:
    payload = path.read_bytes()
    if not payload or len(payload) > MAX_JSON_SIZE:
        raise ValueError(f"JSON size must be 1..{MAX_JSON_SIZE} bytes")
    json.loads(payload.decode("utf-8"))
    return payload


def run_negative(port: str, valid_payload: bytes) -> bool:
    bad_crc = (zlib.crc32(valid_payload) + 1) & 0xFFFFFFFF
    malformed_json = b'{"schema_version":1,"profiles":['
    unsupported_schema = json.dumps({
        "schema_version": 255,
        "config_version": "negative-test",
        "profiles": [{"product_num": 1, "name": "test", "fields": []}],
    }, separators=(",", ":")).encode("utf-8")

    cases = [
        (Request("bad magic", magic=b"BADCFG1"), "NAK:Bad magic", False),
        (Request("zero length", declared_length=0, declared_crc32=0),
         "NAK:Length out of range", False),
        (Request("oversize length",
                 declared_length=MAX_JSON_SIZE + 1,
                 declared_crc32=0),
         "NAK:Length out of range", False),
        (Request("CRC32 mismatch", payload=valid_payload,
                 declared_crc32=bad_crc),
         "NAK:CRC32 mismatch", False),
        (Request("malformed JSON", payload=malformed_json), "NAK:", True),
        (Request("unsupported schema", payload=unsupported_schema),
         "NAK:Unsupported schema_version", False),
    ]
    passed = True
    for request, expected, prefix in cases:
        if not expect(port, request, expected, prefix):
            passed = False
    return passed


def run_retry(port: str, valid_payload: bytes) -> bool:
    bad_crc = (zlib.crc32(valid_payload) + 1) & 0xFFFFFFFF
    rejected = expect(
        port,
        Request("reject bad CRC before retry", payload=valid_payload,
                declared_crc32=bad_crc),
        "NAK:CRC32 mismatch")
    if not rejected:
        return False
    return expect(
        port,
        Request("valid upload without leaving CONFIG page",
                payload=valid_payload),
        "ACK")


def run_repeat(port: str, valid_payload: bytes, count: int) -> bool:
    for index in range(1, count + 1):
        input(
            f"[{index}/{count}] Put the device on CONFIG UPLOAD, then press "
            "Enter...")
        if not expect(
                port,
                Request(f"valid upload {index}/{count}",
                        payload=valid_payload),
                "ACK"):
            return False
        if index < count:
            print("  Device is restarting; wait for the dashboard.")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run ESPCFG1 robustness tests")
    parser.add_argument("port", help="USB CDC port, for example COM12")
    parser.add_argument("json_file", type=Path,
                        help="known-good sensor_profiles.json")
    parser.add_argument(
        "--suite", choices=("negative", "retry", "repeat"),
        default="negative",
        help="negative preserves the installed config; retry/repeat install "
             "the supplied valid JSON")
    parser.add_argument("--count", type=int, default=5,
                        help="valid uploads for the repeat suite")
    args = parser.parse_args()

    if args.count < 1:
        parser.error("--count must be at least 1")

    try:
        payload = load_valid_payload(args.json_file)
        if args.suite == "negative":
            passed = run_negative(args.port, payload)
        elif args.suite == "retry":
            passed = run_retry(args.port, payload)
        else:
            passed = run_repeat(args.port, payload, args.count)
    except (OSError, UnicodeError, ValueError,
            json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    print("\nRESULT:", "PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
