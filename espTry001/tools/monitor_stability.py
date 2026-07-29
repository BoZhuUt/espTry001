#!/usr/bin/env python3
"""Read the local Modbus stability registers and append fixed CSV samples."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import pathlib
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("Missing pyserial. Install it with: python -m pip install pyserial")
    raise SystemExit(2)


SLAVE_ID = 247
START_ADDRESS = 7  # Documentation address 40008.
REGISTER_COUNT = 36  # 40008 through 40043.

CSV_FIELDS = [
    "timestamp",
    "uptime_ms",
    "app_mode",
    "rs485_mode",
    "last_modbus_error",
    "rs_command_depth",
    "rs_command_peak",
    "rs_event_depth",
    "rs_event_peak",
    "rs_dropped_events",
    "pass_request_drops",
    "pass_response_drops",
    "rs_stack_high_water",
    "storage_state",
    "recording",
    "storage_queue_depth",
    "storage_queue_peak",
    "storage_dropped_snapshots",
    "storage_command_failures",
    "storage_stack_high_water",
    "free_heap",
    "minimum_free_heap",
    "log_record_count",
    "last_flush_ms",
    "export_state",
    "alerts",
]


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def make_request() -> bytes:
    frame = struct.pack(
        ">BBHH", SLAVE_ID, 0x03, START_ADDRESS, REGISTER_COUNT
    )
    return frame + struct.pack("<H", crc16(frame))


def read_exact(port: serial.Serial, length: int) -> bytes:
    result = bytearray()
    deadline = time.monotonic() + port.timeout
    while len(result) < length and time.monotonic() < deadline:
        chunk = port.read(length - len(result))
        if chunk:
            result.extend(chunk)
    if len(result) != length:
        raise TimeoutError(f"response timeout: {len(result)}/{length} bytes")
    return bytes(result)


def read_registers(port: serial.Serial) -> list[int]:
    port.reset_input_buffer()
    port.write(make_request())
    port.flush()

    header = read_exact(port, 3)
    if header[0] != SLAVE_ID or header[1] != 0x03:
        raise ValueError(f"unexpected response header: {header.hex(' ')}")
    if header[2] != REGISTER_COUNT * 2:
        raise ValueError(f"unexpected byte count: {header[2]}")

    tail = read_exact(port, header[2] + 2)
    response = header + tail
    if crc16(response[:-2]) != struct.unpack("<H", response[-2:])[0]:
        raise ValueError("response CRC mismatch")
    return list(struct.unpack(f">{REGISTER_COUNT}H", response[3:-2]))


def uint32(registers: list[int], offset: int) -> int:
    return (registers[offset] << 16) | registers[offset + 1]


def decode(registers: list[int]) -> dict[str, int | str]:
    return {
        "timestamp": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "app_mode": registers[0],
        "rs485_mode": registers[1],
        "last_modbus_error": registers[2],
        "rs_command_depth": registers[3],
        "rs_command_peak": registers[4],
        "rs_event_depth": registers[5],
        "rs_event_peak": registers[6],
        "rs_dropped_events": uint32(registers, 7),
        "pass_request_drops": uint32(registers, 9),
        "pass_response_drops": uint32(registers, 11),
        "rs_stack_high_water": uint32(registers, 13),
        "storage_state": registers[15],
        "recording": registers[16],
        "storage_queue_depth": registers[17],
        "storage_queue_peak": registers[18],
        "storage_dropped_snapshots": uint32(registers, 19),
        "storage_command_failures": uint32(registers, 21),
        "storage_stack_high_water": uint32(registers, 23),
        "free_heap": uint32(registers, 25),
        "minimum_free_heap": uint32(registers, 27),
        "log_record_count": uint32(registers, 29),
        "last_flush_ms": uint32(registers, 31),
        "uptime_ms": uint32(registers, 33),
        "export_state": registers[35],
        "alerts": "",
    }


def detect_alerts(current: dict, previous: dict | None) -> str:
    alerts: list[str] = []
    if previous is not None and current["uptime_ms"] < previous["uptime_ms"]:
        alerts.append("DEVICE_RESTART")
    for key in (
        "rs_dropped_events",
        "pass_request_drops",
        "pass_response_drops",
        "storage_dropped_snapshots",
        "storage_command_failures",
    ):
        if previous is not None and current[key] > previous[key]:
            alerts.append(key.upper())
    if current["rs_stack_high_water"] < 1024:
        alerts.append("RS_STACK_LOW")
    if current["storage_stack_high_water"] < 1024:
        alerts.append("STORAGE_STACK_LOW")
    return "|".join(alerts)


def load_last_sample(path: pathlib.Path) -> dict | None:
    if not path.exists() or path.stat().st_size == 0:
        return None

    last_row = None
    with path.open("r", newline="", encoding="utf-8-sig") as csv_file:
        reader = csv.DictReader(csv_file)
        if reader.fieldnames != CSV_FIELDS:
            raise ValueError(
                "existing CSV header does not match this monitor version"
            )
        for row in reader:
            last_row = row
    if last_row is None:
        return None

    sample: dict[str, int | str] = {}
    for field in CSV_FIELDS:
        value = last_row[field]
        sample[field] = (
            value if field in ("timestamp", "alerts") else int(value)
        )
    return sample


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Monitor V1.0.26+ stability registers over USB Modbus."
    )
    parser.add_argument("port", help="USB serial port, for example COM21")
    parser.add_argument(
        "--interval", type=float, default=60.0, help="sample interval in seconds"
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.0,
        help="run duration in hours; 0 means until Ctrl+C",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=None,
        help="CSV output path; default includes the current timestamp",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.interval <= 0 or args.duration < 0:
        print("interval must be > 0 and duration must be >= 0")
        return 2

    output = args.output
    if output is None:
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        output = pathlib.Path(f"stability_{stamp}.csv")
    output.parent.mkdir(parents=True, exist_ok=True)

    deadline = (
        time.monotonic() + args.duration * 3600 if args.duration > 0 else None
    )
    try:
        previous = load_last_sample(output)
    except (OSError, ValueError) as error:
        print(f"Cannot resume output CSV: {error}", file=sys.stderr)
        return 2
    append = previous is not None
    request_failures = 0

    with serial.Serial(args.port, 115200, timeout=2.0) as port, output.open(
        "a", newline="", encoding="utf-8-sig"
    ) as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
        if not append:
            writer.writeheader()
        action = "Appending" if append else "Monitoring"
        print(f"{action} {args.port}; CSV: {output.resolve()}")

        try:
            while deadline is None or time.monotonic() < deadline:
                cycle_start = time.monotonic()
                try:
                    sample = decode(read_registers(port))
                    sample["alerts"] = detect_alerts(sample, previous)
                    writer.writerow(sample)
                    csv_file.flush()
                    previous = sample
                    request_failures = 0
                    print(
                        f"{sample['timestamp']} uptime={sample['uptime_ms']} "
                        f"heap={sample['free_heap']}/{sample['minimum_free_heap']} "
                        f"stack={sample['rs_stack_high_water']}/"
                        f"{sample['storage_stack_high_water']} "
                        f"lost={sample['rs_dropped_events']}/"
                        f"{sample['storage_dropped_snapshots']} "
                        f"{sample['alerts'] or 'OK'}"
                    )
                except (OSError, TimeoutError, ValueError) as error:
                    request_failures += 1
                    print(
                        f"{dt.datetime.now().isoformat(timespec='seconds')} "
                        f"READ_ERROR[{request_failures}]: {error}",
                        file=sys.stderr,
                    )

                remaining = args.interval - (time.monotonic() - cycle_start)
                if remaining > 0:
                    time.sleep(remaining)
        except KeyboardInterrupt:
            print("Stopped by user.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
