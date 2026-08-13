"""Validate normalized calibration profiles and build the LittleFS bundle."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
PROFILE_ID_RE = re.compile(r"^[A-Za-z0-9._-]+$")
OUTCOMES = {"success", "failure", "pending", "info"}
WRITE_TYPES = {"uint16", "float32_cdab"}
INPUT_TYPES = {"uint16", "float32"}
OPERATORS = {"eq", "lt", "lte", "gt", "gte"}
CONDITION_FIELDS = {"hardware_version", "software_version", "parameter_version"}
PROCEDURE_CATEGORIES = {"sensor_calibration", "output_span"}


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def check(condition: bool, path: str, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(f"{path}: {message}")


def validate_condition(value: Any, path: str, errors: list[str]) -> None:
    if value is None:
        return
    check(isinstance(value, dict), path, "condition must be an object", errors)
    if not isinstance(value, dict):
        return
    if set(value) == {"not"}:
        validate_condition(value["not"], f"{path}.not", errors)
        return
    check(set(value) == {"all"}, path, "condition must contain only all or not", errors)
    predicates = value.get("all")
    check(isinstance(predicates, list) and bool(predicates), path, "all must be non-empty", errors)
    if not isinstance(predicates, list):
        return
    for index, predicate in enumerate(predicates):
        item_path = f"{path}.all[{index}]"
        check(isinstance(predicate, dict), item_path, "predicate must be an object", errors)
        if not isinstance(predicate, dict):
            continue
        check(predicate.get("field") in CONDITION_FIELDS, item_path, "invalid field", errors)
        check(predicate.get("operator") in OPERATORS, item_path, "invalid operator", errors)
        check(
            isinstance(predicate.get("value"), int)
            and not isinstance(predicate.get("value"), bool),
            item_path,
            "value must be an integer",
            errors,
        )


def validate_result(value: Any, path: str, errors: list[str]) -> None:
    if value is None:
        return
    check(isinstance(value, dict), path, "result must be an object or null", errors)
    if not isinstance(value, dict):
        return
    address = value.get("address")
    check(isinstance(address, int) and 1 <= address <= 65536, path, "invalid address", errors)
    check(value.get("type") == "uint16", path, "only uint16 result is supported", errors)
    check(value.get("register_count") == 1, path, "register_count must be 1", errors)
    check(
        isinstance(value.get("poll_interval_ms"), int)
        and 50 <= value["poll_interval_ms"] <= 60_000,
        path,
        "invalid poll_interval_ms",
        errors,
    )
    check(
        isinstance(value.get("timeout_ms"), int)
        and value["timeout_ms"] >= value.get("poll_interval_ms", 0),
        path,
        "invalid timeout_ms",
        errors,
    )
    mappings = value.get("mappings")
    check(isinstance(mappings, list) and 1 <= len(mappings) <= 8, path, "invalid mappings", errors)
    if not isinstance(mappings, list):
        return
    seen: set[int] = set()
    terminal_count = 0
    success_count = 0
    for index, mapping in enumerate(mappings):
        item_path = f"{path}.mappings[{index}]"
        check(isinstance(mapping, dict), item_path, "mapping must be an object", errors)
        if not isinstance(mapping, dict):
            continue
        register_value = mapping.get("value")
        check(
            isinstance(register_value, int)
            and not isinstance(register_value, bool)
            and 0 <= register_value <= 65535,
            item_path,
            "value must be UInt16",
            errors,
        )
        if isinstance(register_value, int):
            check(register_value not in seen, item_path, "duplicate value", errors)
            seen.add(register_value)
        outcome = mapping.get("outcome")
        check(outcome in OUTCOMES, item_path, "invalid outcome", errors)
        terminal_count += outcome in ("success", "failure")
        success_count += outcome == "success"
        check(bool(str(mapping.get("message") or "").strip()), item_path, "message is empty", errors)
    check(terminal_count >= 1, path, "at least one terminal mapping is required", errors)
    check(success_count >= 1, path, "at least one success mapping is required", errors)


def validate_profile(profile: Any, source_name: str) -> list[str]:
    errors: list[str] = []
    root = source_name
    check(isinstance(profile, dict), root, "root must be an object", errors)
    if not isinstance(profile, dict):
        return errors
    check(profile.get("schema_version") == SCHEMA_VERSION, root, "schema_version must be 1", errors)
    profile_id = profile.get("profile_id")
    check(
        isinstance(profile_id, str) and PROFILE_ID_RE.fullmatch(profile_id) is not None,
        root,
        "invalid profile_id",
        errors,
    )
    product_num = profile.get("product_num")
    check(
        isinstance(product_num, int)
        and not isinstance(product_num, bool)
        and 1 <= product_num <= 0xFFFFFFFF,
        root,
        "invalid product_num",
        errors,
    )
    check(profile.get("release_status") in {"approved", "draft", "blocked"}, root, "invalid release_status", errors)
    procedures = profile.get("procedures")
    check(isinstance(procedures, list) and 1 <= len(procedures) <= 16, root, "invalid procedures", errors)
    if not isinstance(procedures, list):
        return errors
    if profile.get("release_status") == "approved":
        sensor_count = sum(
            procedure.get("category") == "sensor_calibration"
            for procedure in procedures
            if isinstance(procedure, dict)
        )
        output_count = sum(
            procedure.get("category") == "output_span"
            for procedure in procedures
            if isinstance(procedure, dict)
        )
        check(
            len(procedures) <= 5,
            root,
            "approved profile exceeds five UI operations",
            errors,
        )
        check(
            sensor_count <= 4,
            root,
            "approved profile exceeds four sensor calibrations",
            errors,
        )
        check(
            output_count <= 1,
            root,
            "approved profile exceeds one output-span operation",
            errors,
        )

    procedure_ids: set[str] = set()
    total_steps = 0
    for procedure_index, procedure in enumerate(procedures):
        path = f"{root}.procedures[{procedure_index}]"
        check(isinstance(procedure, dict), path, "procedure must be an object", errors)
        if not isinstance(procedure, dict):
            continue
        procedure_id = procedure.get("id")
        check(isinstance(procedure_id, str) and bool(procedure_id), path, "id is empty", errors)
        if isinstance(procedure_id, str):
            check(procedure_id not in procedure_ids, path, "duplicate id", errors)
            procedure_ids.add(procedure_id)
        check(
            procedure.get("category") in PROCEDURE_CATEGORIES,
            path,
            "invalid category",
            errors,
        )
        validate_condition(procedure.get("when"), f"{path}.when", errors)

        inputs = procedure.get("inputs")
        check(isinstance(inputs, list) and len(inputs) <= 3, path, "invalid inputs", errors)
        input_ids: set[str] = set()
        if isinstance(inputs, list):
            for input_index, item in enumerate(inputs):
                item_path = f"{path}.inputs[{input_index}]"
                check(isinstance(item, dict), item_path, "input must be an object", errors)
                if not isinstance(item, dict):
                    continue
                input_id = item.get("id")
                check(isinstance(input_id, str) and bool(input_id), item_path, "invalid id", errors)
                if isinstance(input_id, str):
                    check(input_id not in input_ids, item_path, "duplicate id", errors)
                    input_ids.add(input_id)
                check(item.get("type") in INPUT_TYPES, item_path, "invalid type", errors)
                minimum = item.get("minimum")
                maximum = item.get("maximum")
                default = item.get("default")
                numeric = all(
                    isinstance(number, (int, float)) and not isinstance(number, bool)
                    for number in (minimum, maximum, default)
                )
                check(numeric, item_path, "bounds/default must be numeric", errors)
                if numeric:
                    min_ok = default >= minimum if item.get("minimum_inclusive") else default > minimum
                    max_ok = default <= maximum if item.get("maximum_inclusive") else default < maximum
                    check(minimum <= maximum and min_ok and max_ok, item_path, "default outside bounds", errors)

        steps = procedure.get("steps")
        check(isinstance(steps, list) and 1 <= len(steps) <= 16, path, "invalid steps", errors)
        if not isinstance(steps, list):
            continue
        total_steps += len(steps)
        for step_index, step in enumerate(steps):
            step_path = f"{path}.steps[{step_index}]"
            check(isinstance(step, dict), step_path, "step must be an object", errors)
            if not isinstance(step, dict):
                continue
            validate_condition(step.get("when"), f"{step_path}.when", errors)
            alternatives = step.get("alternatives")
            check(
                isinstance(alternatives, list) and 1 <= len(alternatives) <= 4,
                step_path,
                "invalid alternatives",
                errors,
            )
            if not isinstance(alternatives, list):
                continue
            for alt_index, alternative in enumerate(alternatives):
                alt_path = f"{step_path}.alternatives[{alt_index}]"
                check(isinstance(alternative, dict), alt_path, "alternative must be object", errors)
                if not isinstance(alternative, dict):
                    continue
                validate_condition(alternative.get("when"), f"{alt_path}.when", errors)
                write = alternative.get("write")
                check(isinstance(write, dict), alt_path, "write must be an object", errors)
                if not isinstance(write, dict):
                    continue
                address = write.get("address")
                write_type = write.get("type")
                check(isinstance(address, int) and 1 <= address <= 65536, alt_path, "invalid write address", errors)
                check(write_type in WRITE_TYPES, alt_path, "invalid write type", errors)
                check(not (write_type == "float32_cdab" and address == 65536), alt_path, "float write overflows", errors)
                source = write.get("value")
                check(isinstance(source, dict), alt_path, "write value must be object", errors)
                if isinstance(source, dict):
                    kind = source.get("kind")
                    check(kind in {"fixed", "input"}, alt_path, "invalid value kind", errors)
                    if kind == "input":
                        check(source.get("input_id") in input_ids, alt_path, "unknown input_id", errors)
                wait_ms = alternative.get("wait_after_ms")
                check(isinstance(wait_ms, int) and 0 <= wait_ms <= 300_000, alt_path, "invalid wait", errors)
                validate_result(alternative.get("result"), f"{alt_path}.result", errors)
    check(total_steps <= 32, root, "profile exceeds 32 command steps", errors)
    return errors


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("config/calibration_profiles"))
    parser.add_argument("--output", type=Path, default=Path("data/calibration"))
    parser.add_argument("--config-version", default="2026.08.13.1")
    parser.add_argument("--validate-only", action="store_true")
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] = ()) -> int:
    args = parse_args(argv)
    profiles: list[dict[str, Any]] = []
    errors: list[str] = []
    for path in sorted(args.input.glob("*.json"), key=lambda item: item.name.lower()):
        try:
            profile = load_json(path)
        except Exception as exc:
            errors.append(f"{path.name}: {exc}")
            continue
        profile_errors = validate_profile(profile, path.name)
        errors.extend(profile_errors)
        if not profile_errors:
            profiles.append(profile)

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    approved = [profile for profile in profiles if profile["release_status"] == "approved"]
    skipped = [profile for profile in profiles if profile["release_status"] != "approved"]
    print(f"Validated {len(profiles)} profile(s): {len(approved)} approved, {len(skipped)} skipped")
    if args.validate_only:
        return 0

    expected_files = {"index.json"} | {
        f"{profile['profile_id']}.json" for profile in approved
    }
    if args.output.exists():
        stale_files = sorted(
            path.name for path in args.output.glob("*.json") if path.name not in expected_files
        )
        if stale_files:
            print(
                "ERROR: stale generated calibration files must be reviewed and removed: "
                + ", ".join(stale_files),
                file=sys.stderr,
            )
            return 1
    args.output.mkdir(parents=True, exist_ok=True)
    index_profiles: list[dict[str, Any]] = []
    for profile in approved:
        file_name = f"{profile['profile_id']}.json"
        write_json(args.output / file_name, profile)
        index_profiles.append(
            {
                "profile_id": profile["profile_id"],
                "product_num": profile["product_num"],
                "variant": profile["variant"],
                "priority": 0,
                "file": f"/calibration/{file_name}",
                "match": None,
                "requires_version_read": profile["requires_version_read"],
            }
        )
    write_json(
        args.output / "index.json",
        {
            "schema_version": SCHEMA_VERSION,
            "config_version": args.config_version,
            "profiles": sorted(index_profiles, key=lambda item: (item["product_num"], item["profile_id"])),
        },
    )

    print(f"Built LittleFS bundle with {len(approved)} approved profile(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
