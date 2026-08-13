import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from import_calibration_profiles import (  # noqa: E402
    ImportContext,
    normalize_bound,
    normalize_profile,
    parse_condition,
)
from build_calibration_bundle import validate_profile  # noqa: E402


class CalibrationImportTests(unittest.TestCase):
    def test_string_bounds_are_structured(self):
        self.assertEqual(normalize_bound(">5", "minimum", "minimum"), (5, False))
        self.assertEqual(normalize_bound(">=0.0", "minimum", "minimum"), (0, True))
        self.assertEqual(normalize_bound("<=0.2", "maximum", "maximum"), (0.2, True))

    def test_condition_parser_structures_and_expression(self):
        self.assertEqual(
            parse_condition("softVersion >= 64 && softVersion < 103"),
            {
                "all": [
                    {"field": "software_version", "operator": "gte", "value": 64},
                    {"field": "software_version", "operator": "lt", "value": 103},
                ]
            },
        )

    def test_wait_only_profile_separates_input_from_command(self):
        source = {
            "PN": 57769,
            "ModelName": "HW-201T",
            "Readings": [
                {
                    "Name": "Sugar",
                    "Unit": "ppm",
                    "Decimal": 2,
                    "Calibrations": [
                        {
                            "Name": "slope_title",
                            "Message": "slope_message",
                            "Cmds": [
                                {
                                    "RegAddress": 44002,
                                    "RegType": "float",
                                    "Editable": True,
                                    "SetValue": 100,
                                    "SetValueMin": 1,
                                    "SetValueMax": 1000,
                                    "Waiting_mS": 100,
                                },
                                {
                                    "RegAddress": 43006,
                                    "RegType": "ushort",
                                    "Editable": False,
                                    "SetValue": 22,
                                    "Waiting_mS": 1000,
                                },
                            ],
                        }
                    ],
                }
            ],
        }
        ctx = ImportContext()
        profile = normalize_profile(source, "57769.json", ctx)
        self.assertIsNotNone(profile)
        procedure = profile["procedures"][0]
        self.assertEqual(procedure["inputs"][0]["id"], "input_1")
        value = procedure["steps"][0]["alternatives"][0]["write"]["value"]
        self.assertEqual(value, {"kind": "input", "input_id": "input_1"})
        self.assertIsNone(procedure["steps"][1]["alternatives"][0]["result"])
        self.assertEqual(profile["release_status"], "approved")

    def test_mapped_result_classifies_pending_success_and_failure(self):
        source = {
            "PN": 12345,
            "ModelName": "Example",
            "Readings": [
                {
                    "Name": "Reading",
                    "Unit": "ppm",
                    "Calibrations": [
                        {
                            "Name": "Calibration",
                            "Message": "Proceed",
                            "Cmds": [
                                {
                                    "RegAddress": 43006,
                                    "RegType": "ushort",
                                    "Editable": False,
                                    "SetValue": 21,
                                    "Waiting_mS": 1000,
                                    "ResultRegAddress": 41003,
                                    "ResultRegType": "ushort",
                                    "Results": [
                                        {"RegValue": 0, "ResultMessage": "Calibration Idle"},
                                        {"RegValue": 1, "ResultMessage": "Calibration succeed."},
                                        {"RegValue": 2, "ResultMessage": "Calibration failed."},
                                        {
                                            "RegValue": None,
                                            "ResultMessage": "Communication failure, please retry.",
                                        },
                                    ],
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        ctx = ImportContext()
        profile = normalize_profile(source, "12345.json", ctx)
        result = profile["procedures"][0]["steps"][0]["alternatives"][0]["result"]
        self.assertEqual(
            [mapping["outcome"] for mapping in result["mappings"]],
            ["pending", "success", "failure"],
        )
        self.assertEqual(
            result["communication_error_message"],
            "Communication failure, please retry.",
        )
        self.assertEqual(profile["release_status"], "draft")
        self.assertTrue(any(issue.severity == "review_required" for issue in ctx.issues))

    def test_conversion_and_procedure_condition_are_normalized(self):
        source = {
            "PN": 12346,
            "ModelName": "Example 2",
            "Readings": [
                {
                    "Name": "ORP",
                    "Unit": "mV",
                    "Decimal": 3,
                    "Calibrations": [
                        {
                            "Name": "ORP Calibration",
                            "Message": "Proceed",
                            "VersionJudgment": "softVersion>=391",
                            "Cmds": [
                                {
                                    "RegAddress": 44002,
                                    "RegType": "float",
                                    "Editable": True,
                                    "SetValue": 229,
                                    "SetValueMin": -1500,
                                    "SetValueMax": 1500,
                                    "Conversion": "value/1000",
                                    "Waiting_mS": 100,
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        profile = normalize_profile(source, "12346.json", ImportContext())
        procedure = profile["procedures"][0]
        self.assertEqual(
            procedure["when"],
            {
                "all": [
                    {"field": "software_version", "operator": "gte", "value": 391}
                ]
            },
        )
        self.assertEqual(
            procedure["inputs"][0]["write_scale"],
            {"numerator": 1, "denominator": 1000},
        )
        self.assertTrue(profile["requires_version_read"])

    def test_conditional_address_override_becomes_exclusive_alternatives(self):
        source = {
            "PN": 54148,
            "ModelName": "ST-605",
            "Readings": [
                {
                    "Name": "ClO2",
                    "Unit": "ppm",
                    "Calibrations": [
                        {
                            "Name": "slope_title",
                            "Message": "Proceed",
                            "Cmds": [
                                {
                                    "RegAddress": 44004,
                                    "RegAddressVersionJudgment": [
                                        {"VersionJudgment": "hardVersion>=20", "value": 44002}
                                    ],
                                    "RegType": "float",
                                    "Editable": True,
                                    "SetValue": 10,
                                    "SetValueMin": 1,
                                    "SetValueMax": 100,
                                    "Waiting_mS": 100,
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        profile = normalize_profile(source, "54148.json", ImportContext())
        alternatives = profile["procedures"][0]["steps"][0]["alternatives"]
        condition = {
            "all": [
                {"field": "hardware_version", "operator": "gte", "value": 20}
            ]
        }
        self.assertEqual(alternatives[0]["when"], {"not": condition})
        self.assertEqual(alternatives[0]["write"]["address"], 44004)
        self.assertEqual(alternatives[1]["when"], condition)
        self.assertEqual(alternatives[1]["write"]["address"], 44002)
        self.assertTrue(profile["requires_version_read"])

    def test_generated_profile_passes_publish_validator(self):
        source = {
            "PN": 57769,
            "ModelName": "HW-201T",
            "Readings": [
                {
                    "Name": "Sugar",
                    "Unit": "ppm",
                    "Calibrations": [
                        {
                            "Name": "zero_title",
                            "Message": "zero_message",
                            "Cmds": [
                                {
                                    "RegAddress": 43006,
                                    "RegType": "ushort",
                                    "Editable": False,
                                    "SetValue": 21,
                                    "Waiting_mS": 1000,
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        profile = normalize_profile(source, "57769.json", ImportContext())
        self.assertEqual(validate_profile(profile, "57769.default.json"), [])


if __name__ == "__main__":
    unittest.main()
