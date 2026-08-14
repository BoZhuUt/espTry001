"""Build and upload the approved calibration bundle through ESPCAL1."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import serial
except ImportError:
    print("Missing pyserial; run: python -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

from build_calibration_package import (
    PackageError,
    build_package,
    collect_approved_profiles,
)
from upload_sensor_profiles import upload


MAGIC = b"ESPCAL1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC port, for example COM12")
    parser.add_argument(
        "--input", type=Path, default=Path("config/calibration_profiles")
    )
    parser.add_argument("--config-version", default="2026.08.14.1")
    parser.add_argument(
        "--save-package",
        type=Path,
        help="optionally save the exact .calpkg payload sent to the device",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        profiles, skipped = collect_approved_profiles(args.input)
        package, _, index = build_package(profiles, args.config_version)
        if args.save_package is not None:
            args.save_package.parent.mkdir(parents=True, exist_ok=True)
            args.save_package.write_bytes(package)

        print(
            f"Bundle {index['bundle_id']}: {len(profiles)} approved, "
            f"{len(skipped)} skipped, {len(package)} bytes"
        )
        print("Published PN: " + ", ".join(str(item["product_num"]) for item in profiles))
        print(f"Uploading to {args.port} ...")
        response = upload(args.port, package, magic=MAGIC)
    except (
        OSError,
        UnicodeError,
        ValueError,
        PackageError,
        RuntimeError,
        TimeoutError,
        serial.SerialException,
    ) as error:
        print(f"Calibration upload failed: {error}", file=sys.stderr)
        return 1

    print(f"Device response: {response}")
    if response != "ACK":
        return 1
    print("Calibration bundle installed; device is restarting.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
