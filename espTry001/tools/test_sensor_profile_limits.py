"""Boundary tests for the sensor profile count and JSON size limits."""

from __future__ import annotations

import json
import unittest

import generate_sensor_profiles as generator


def make_profile(product_num: int, field_count: int = 1) -> dict:
    fields = []
    for index in range(field_count):
        fields.append({
            "key": f"field_{index:02d}_xxxxxxxxxxxxxx",
            "label": f"Field {index:02d} xxxxxxxxxxxxxx",
            "function": 3,
            "address": 1000 + index * 2,
            "address_base": 1,
            "type": "float32",
            "word_order": "CDAB",
            "decimals": 3,
        })
    return {
        "product_num": product_num,
        "name": f"Profile {product_num:05d}",
        "fields": fields,
    }


def make_config(profile_count: int, field_count: int = 1) -> dict:
    return {
        "schema_version": 1,
        "config_version": "limit-test",
        "profiles": [
            make_profile(10000 + index, field_count)
            for index in range(profile_count)
        ],
    }


class SensorProfileLimitTests(unittest.TestCase):
    def test_fifty_profiles_are_accepted(self) -> None:
        profiles = generator.validate_config(make_config(50))
        self.assertEqual(len(profiles), 50)

    def test_fifty_one_profiles_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "50"):
            generator.validate_config(make_config(51))

    def test_maximum_shape_fits_json_limit(self) -> None:
        config = make_config(50, generator.MAX_FIELDS)
        generator.validate_config(config)
        payload = (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )
        self.assertLessEqual(len(payload), generator.MAX_JSON_FILE_SIZE)

    def test_json_limit_is_256_kib(self) -> None:
        self.assertEqual(generator.MAX_JSON_FILE_SIZE, 256 * 1024)


if __name__ == "__main__":
    unittest.main()
