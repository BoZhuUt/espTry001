"""Build a deterministic hot-update package from approved calibration profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from build_calibration_bundle import load_json, validate_profile


PACKAGE_MAGIC = b"CALPKG1"
PACKAGE_EXTENSION = ".calpkg"
MAX_PROFILES = 32
MAX_PATH_BYTES = 79
MAX_PROFILE_BYTES = 64 * 1024
MAX_INDEX_BYTES = 16 * 1024
MAX_PACKAGE_BYTES = 512 * 1024
ENTRY_PROFILE = 1
ENTRY_INDEX = 2
ENTRY_HEADER = struct.Struct("<BBII")
COUNT_HEADER = struct.Struct("<H")


class PackageError(ValueError):
    """Raised when source profiles cannot form a safe publish package."""


@dataclass(frozen=True)
class PackageEntry:
    kind: int
    path: str
    payload: bytes

    @property
    def crc32(self) -> int:
        return zlib.crc32(self.payload) & 0xFFFFFFFF


def canonical_json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def collect_approved_profiles(input_dir: Path) -> tuple[list[dict[str, Any]], list[str]]:
    profiles: list[dict[str, Any]] = []
    skipped: list[str] = []
    errors: list[str] = []
    for path in sorted(input_dir.glob("*.json"), key=lambda item: item.name.lower()):
        try:
            profile = load_json(path)
        except Exception as exc:
            errors.append(f"{path.name}: {exc}")
            continue
        errors.extend(validate_profile(profile, path.name))
        if isinstance(profile, dict) and profile.get("release_status") == "approved":
            profiles.append(profile)
        else:
            skipped.append(path.name)

    seen_pn: dict[int, str] = {}
    for profile in profiles:
        product_num = profile["product_num"]
        previous = seen_pn.get(product_num)
        if previous is not None:
            errors.append(
                f"duplicate approved PN {product_num}: {previous}, {profile['profile_id']}"
            )
        seen_pn[product_num] = profile["profile_id"]
    if len(profiles) > MAX_PROFILES:
        errors.append(f"approved profile count exceeds {MAX_PROFILES}")
    if errors:
        raise PackageError("\n".join(errors))
    return sorted(profiles, key=lambda item: (item["product_num"], item["profile_id"])), skipped


def _profile_entry(profile: dict[str, Any]) -> PackageEntry:
    payload = canonical_json_bytes(profile)
    if not payload or len(payload) > MAX_PROFILE_BYTES:
        raise PackageError(f"{profile['profile_id']}: profile payload size invalid")
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    path = f"/calibration/p{profile['product_num']}-{crc:08x}.json"
    return PackageEntry(ENTRY_PROFILE, path, payload)


def build_package(
    profiles: list[dict[str, Any]], config_version: str
) -> tuple[bytes, list[PackageEntry], dict[str, Any]]:
    if not profiles:
        raise PackageError("at least one approved calibration profile is required")
    if len(profiles) > MAX_PROFILES:
        raise PackageError(f"approved profile count exceeds {MAX_PROFILES}")
    if not config_version or len(config_version.encode("utf-8")) > 24:
        raise PackageError("config_version must be 1..24 UTF-8 bytes")

    profile_entries = [_profile_entry(profile) for profile in profiles]
    digest = hashlib.sha256()
    for entry in profile_entries:
        digest.update(entry.path.encode("ascii"))
        digest.update(entry.payload)
    bundle_id = digest.hexdigest()[:16]

    index_profiles: list[dict[str, Any]] = []
    for profile, entry in zip(profiles, profile_entries):
        index_profiles.append(
            {
                "profile_id": profile["profile_id"],
                "product_num": profile["product_num"],
                "variant": profile["variant"],
                "priority": 0,
                "file": entry.path,
                "match": None,
                "requires_version_read": profile["requires_version_read"],
            }
        )
    index = {
        "schema_version": 1,
        "config_version": config_version,
        "bundle_id": bundle_id,
        "profiles": index_profiles,
    }
    index_payload = canonical_json_bytes(index)
    if len(index_payload) > MAX_INDEX_BYTES:
        raise PackageError("generated calibration index exceeds firmware limit")
    entries = profile_entries + [
        PackageEntry(ENTRY_INDEX, "/calibration/index.json", index_payload)
    ]

    encoded = bytearray(PACKAGE_MAGIC)
    encoded.extend(COUNT_HEADER.pack(len(entries)))
    for entry in entries:
        path_bytes = entry.path.encode("ascii")
        if not path_bytes or len(path_bytes) > MAX_PATH_BYTES:
            raise PackageError(f"package path length invalid: {entry.path}")
        encoded.extend(
            ENTRY_HEADER.pack(
                entry.kind, len(path_bytes), len(entry.payload), entry.crc32
            )
        )
        encoded.extend(path_bytes)
        encoded.extend(entry.payload)
    if len(encoded) > MAX_PACKAGE_BYTES:
        raise PackageError(
            f"calibration package exceeds {MAX_PACKAGE_BYTES} bytes"
        )
    return bytes(encoded), entries, index


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", type=Path, default=Path("config/calibration_profiles")
    )
    parser.add_argument(
        "--output", type=Path, default=Path("build/calibration_bundle.calpkg")
    )
    parser.add_argument("--config-version", default="2026.08.14.1")
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] = ()) -> int:
    args = parse_args(argv)
    try:
        profiles, skipped = collect_approved_profiles(args.input)
        package, entries, index = build_package(profiles, args.config_version)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(package)
    except (OSError, PackageError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(
        f"Built {args.output} ({len(package)} bytes, "
        f"{len(profiles)} approved, {len(skipped)} skipped, "
        f"bundle {index['bundle_id']})"
    )
    for entry in entries:
        print(f"  {entry.path}  {len(entry.payload)} bytes  {entry.crc32:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
