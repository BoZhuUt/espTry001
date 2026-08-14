import copy
import struct
import sys
import unittest
import zlib
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_calibration_package import (  # noqa: E402
    COUNT_HEADER,
    ENTRY_HEADER,
    ENTRY_INDEX,
    ENTRY_PROFILE,
    PACKAGE_MAGIC,
    PackageError,
    build_package,
)


def approved_profile(product_num: int = 10009) -> dict:
    operation_id = "calibration_1"
    return {
        "schema_version": 1,
        "profile_id": f"{product_num}.default",
        "product_num": product_num,
        "variant": "default",
        "model_name": "Test",
        "requires_version_read": False,
        "release_status": "approved",
        "review": {
            "input_ranges": True,
            "result_mappings": True,
            "version_conditions": True,
            "command_sequences": True,
            "confirmed_operations": [operation_id],
        },
        "audit_issues": [],
        "procedures": [
            {
                "id": operation_id,
                "category": "sensor_calibration",
                "reading_name": "Reading",
                "name": "Calibration",
                "message": "Proceed",
                "unit": "ppm",
                "when": None,
                "inputs": [],
                "steps": [
                    {
                        "id": "step_1",
                        "when": None,
                        "alternatives": [
                            {
                                "when": None,
                                "write": {
                                    "address": 43006,
                                    "type": "uint16",
                                    "value": {"kind": "fixed", "number": 1},
                                },
                                "wait_after_ms": 100,
                                "result": None,
                            }
                        ],
                    }
                ],
            }
        ],
    }


def decode_entries(package: bytes) -> list[tuple[int, str, bytes, int]]:
    assert package.startswith(PACKAGE_MAGIC)
    offset = len(PACKAGE_MAGIC)
    count = COUNT_HEADER.unpack_from(package, offset)[0]
    offset += COUNT_HEADER.size
    entries = []
    for _ in range(count):
        kind, path_length, length, expected_crc = ENTRY_HEADER.unpack_from(
            package, offset
        )
        offset += ENTRY_HEADER.size
        path = package[offset : offset + path_length].decode("ascii")
        offset += path_length
        payload = package[offset : offset + length]
        offset += length
        entries.append((kind, path, payload, expected_crc))
    if offset != len(package):
        raise AssertionError("trailing package data")
    return entries


class CalibrationPackageTests(unittest.TestCase):
    def test_package_is_deterministic_and_index_is_last(self):
        profiles = [approved_profile(10009), approved_profile(50661)]
        first, _, first_index = build_package(profiles, "test.1")
        second, _, second_index = build_package(copy.deepcopy(profiles), "test.1")
        self.assertEqual(first, second)
        self.assertEqual(first_index, second_index)
        entries = decode_entries(first)
        self.assertEqual([entry[0] for entry in entries], [ENTRY_PROFILE, ENTRY_PROFILE, ENTRY_INDEX])
        self.assertEqual(entries[-1][1], "/calibration/index.json")
        for _, _, payload, expected_crc in entries:
            self.assertEqual(zlib.crc32(payload) & 0xFFFFFFFF, expected_crc)

    def test_profile_paths_are_versioned_by_content_crc(self):
        profile = approved_profile()
        _, entries_before, _ = build_package([profile], "test.1")
        profile["procedures"][0]["message"] = "Changed"
        _, entries_after, _ = build_package([profile], "test.1")
        self.assertNotEqual(entries_before[0].path, entries_after[0].path)
        self.assertTrue(entries_before[0].path.startswith("/calibration/p10009-"))

    def test_empty_publish_is_rejected(self):
        with self.assertRaises(PackageError):
            build_package([], "test.1")

    def test_profile_limit_is_enforced(self):
        profiles = [approved_profile(10000 + index) for index in range(33)]
        with self.assertRaises(PackageError):
            build_package(profiles, "test.1")


if __name__ == "__main__":
    unittest.main()
