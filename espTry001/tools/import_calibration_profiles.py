"""Import mobile-app device JSON into the normalized calibration Schema v1.

The source directory is read-only. The importer writes normalized per-profile
files and an audit report inside this project. It uses only Python's standard
library so it can share validation rules with the existing build workflow.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
MAX_PROCEDURES = 16
MAX_INPUTS_PER_PROCEDURE = 3
MAX_STEPS_PER_PROCEDURE = 16
MAX_COMMANDS_PER_PROFILE = 32
MAX_RESULT_MAPPINGS = 8
DEFAULT_RESULT_POLL_MS = 500
DEFAULT_RESULT_TIMEOUT_MS = 120_000

CONDITION_FIELDS = {
    "hardVersion": "hardware_version",
    "softVersion": "software_version",
    "paramVersion": "parameter_version",
}
CONDITION_OPERATORS = {
    "==": "eq",
    "<": "lt",
    "<=": "lte",
    ">": "gt",
    ">=": "gte",
}
CONDITION_RE = re.compile(
    r"^(hardVersion|softVersion|paramVersion)\s*(==|<=|>=|<|>)\s*(\d+)$"
)
SUCCESS_RE = re.compile(r"succeed|success|successful|complete", re.IGNORECASE)
PENDING_RE = re.compile(r"in\s+process|in\s+progress|idle", re.IGNORECASE)
SAFE_ID_RE = re.compile(r"[^a-z0-9._-]+")
BOUND_RE = re.compile(r"^(<=|>=|<|>)\s*(-?(?:\d+(?:\.\d*)?|\.\d+))$")


@dataclass
class Issue:
    severity: str
    file: str
    product_num: int | None
    path: str
    message: str


class ImportContext:
    def __init__(self) -> None:
        self.issues: list[Issue] = []

    def add(
        self,
        severity: str,
        file: str,
        product_num: int | None,
        path: str,
        message: str,
    ) -> None:
        self.issues.append(Issue(severity, file, product_num, path, message))


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def require_int(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{label} must be an integer")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str):
        try:
            result = int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{label} must be an integer") from exc
    else:
        raise ValueError(f"{label} must be an integer")
    if result < minimum or result > maximum:
        raise ValueError(f"{label} must be in {minimum}..{maximum}")
    return result


def require_number(value: Any, label: str) -> float | int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be numeric")
    if not math.isfinite(float(value)):
        raise ValueError(f"{label} must be finite")
    return value


def normalize_bound(value: Any, label: str, side: str) -> tuple[float | int, bool]:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return require_number(value, label), True
    if not isinstance(value, str):
        raise ValueError(f"{label} must be numeric or an explicit bound")
    match = BOUND_RE.fullmatch(value.strip())
    if match is None:
        raise ValueError(f"{label} has unsupported bound syntax: {value!r}")
    operator, raw_number = match.groups()
    if side == "minimum" and operator not in (">", ">="):
        raise ValueError(f"{label} must use > or >=")
    if side == "maximum" and operator not in ("<", "<="):
        raise ValueError(f"{label} must use < or <=")
    number = float(raw_number)
    if number.is_integer():
        number = int(number)
    return number, operator in (">=", "<=")


def safe_id(value: str, fallback: str) -> str:
    normalized = SAFE_ID_RE.sub("_", value.lower()).strip("._-")
    return normalized or fallback


def parse_condition(expression: Any) -> dict[str, Any] | None:
    if expression is None or str(expression).strip() == "":
        return None
    predicates: list[dict[str, Any]] = []
    for raw_part in str(expression).split("&&"):
        part = raw_part.strip()
        match = CONDITION_RE.fullmatch(part)
        if match is None:
            raise ValueError(f"unsupported version expression: {expression!r}")
        field, operator, value = match.groups()
        predicates.append(
            {
                "field": CONDITION_FIELDS[field],
                "operator": CONDITION_OPERATORS[operator],
                "value": int(value),
            }
        )
    return {"all": predicates}


def merge_conditions(*conditions: dict[str, Any] | None) -> dict[str, Any] | None:
    predicates: list[dict[str, Any]] = []
    for condition in conditions:
        if condition:
            predicates.extend(condition["all"])
    return {"all": predicates} if predicates else None


def normalize_address_override(value: Any, label: str) -> tuple[dict[str, Any], int] | None:
    if value is None:
        return None
    if not isinstance(value, list) or len(value) != 1 or not isinstance(value[0], dict):
        raise ValueError(f"{label} must contain exactly one conditional override")
    item = value[0]
    condition = parse_condition(item.get("VersionJudgment"))
    if condition is None:
        raise ValueError(f"{label} requires VersionJudgment")
    address = require_int(item.get("value"), f"{label}.value", 1, 65536)
    return condition, address


def normalize_scale(conversion: Any) -> dict[str, int]:
    if conversion is None or str(conversion).strip() == "":
        return {"numerator": 1, "denominator": 1}
    compact = str(conversion).replace(" ", "")
    if compact == "value/100":
        return {"numerator": 1, "denominator": 100}
    if compact == "value/1000":
        return {"numerator": 1, "denominator": 1000}
    raise ValueError(f"unsupported Conversion: {conversion!r}")


def infer_decimals(reading: dict[str, Any], value_type: str) -> int:
    if value_type == "uint16":
        return 0
    value = reading.get("Decimal", 2)
    try:
        return max(0, min(6, int(value)))
    except (TypeError, ValueError):
        return 2


def normalize_unit(
    reading: dict[str, Any], calibration: dict[str, Any], command: dict[str, Any]
) -> str:
    dynamic = command.get("DynamicUnit")
    if dynamic not in (None, "", "Unit"):
        raise ValueError(f"unsupported DynamicUnit: {dynamic!r}")
    if dynamic == "Unit":
        return str(reading.get("Unit") or "")
    return str(
        command.get("Unit")
        or calibration.get("Unit")
        or reading.get("Unit")
        or ""
    )


def classify_result(message: str) -> str:
    if SUCCESS_RE.search(message):
        return "success"
    if PENDING_RE.search(message):
        return "pending"
    return "failure"


def normalize_result(
    command: dict[str, Any],
    ctx: ImportContext,
    file_name: str,
    product_num: int,
    path: str,
) -> dict[str, Any] | None:
    raw_address = command.get("ResultRegAddress")
    if raw_address in (None, "", 0, "0"):
        return None
    address = require_int(raw_address, "ResultRegAddress", 1, 65536)
    result_type = str(command.get("ResultRegType") or "")
    if result_type != "ushort":
        raise ValueError(f"unsupported ResultRegType: {result_type!r}")
    register_count = require_int(
        command.get("ResultRegLen", 1), "ResultRegLen", 0, 255
    )
    if register_count != 1:
        ctx.add(
            "unsupported",
            file_name,
            product_num,
            path,
            f"ResultRegLen={register_count} has ambiguous layout; only 1 is supported",
        )

    raw_mappings = command.get("Results") or []
    if isinstance(raw_mappings, dict):
        raw_mappings = [raw_mappings]
    if not isinstance(raw_mappings, list) or not raw_mappings:
        raise ValueError("non-zero ResultRegAddress requires Results[]")
    mappings: list[dict[str, Any]] = []
    seen_values: set[int] = set()
    success_count = 0
    terminal_count = 0
    communication_error_message: str | None = None
    for index, raw in enumerate(raw_mappings):
        if not isinstance(raw, dict):
            raise ValueError(f"Results[{index}] must be an object")
        message = str(raw.get("ResultMessage") or "").strip()
        if not message:
            raise ValueError(f"Results[{index}].ResultMessage cannot be empty")
        if raw.get("RegValue") is None:
            if communication_error_message is not None:
                raise ValueError("Results[] contains multiple null RegValue entries")
            communication_error_message = message
            continue
        value = require_int(raw.get("RegValue"), "RegValue", 0, 65535)
        if value in seen_values:
            raise ValueError(f"duplicate result RegValue {value}")
        seen_values.add(value)
        outcome = classify_result(message)
        success_count += outcome == "success"
        terminal_count += outcome in ("success", "failure")
        mappings.append({"value": value, "outcome": outcome, "message": message})

    if len(mappings) > MAX_RESULT_MAPPINGS:
        raise ValueError(
            f"result mapping count {len(mappings)} exceeds {MAX_RESULT_MAPPINGS}"
        )
    if not mappings:
        raise ValueError("Results[] has no register value mappings")
    if success_count != 1:
        raise ValueError(f"expected exactly one auto-detected success, got {success_count}")
    if terminal_count == 0:
        raise ValueError("result map has no terminal outcome")
    ctx.add(
        "review_required",
        file_name,
        product_num,
        path,
        "result outcomes were auto-classified and require approval before release",
    )
    result = {
        "address": address,
        "type": "uint16",
        "register_count": register_count,
        "poll_interval_ms": DEFAULT_RESULT_POLL_MS,
        "timeout_ms": DEFAULT_RESULT_TIMEOUT_MS,
        "mappings": mappings,
    }
    if communication_error_message is not None:
        result["communication_error_message"] = communication_error_message
    return result


def normalize_command(
    reading: dict[str, Any],
    calibration: dict[str, Any],
    command: dict[str, Any],
    input_index: int,
    ctx: ImportContext,
    file_name: str,
    product_num: int,
    path: str,
) -> tuple[dict[str, Any], dict[str, Any] | None, bool]:
    source_type = str(command.get("RegType") or "")
    if source_type == "ushort":
        write_type = "uint16"
        input_type = "uint16"
    elif source_type == "float":
        write_type = "float32_cdab"
        input_type = "float32"
    else:
        raise ValueError(f"unsupported RegType: {source_type!r}")

    address = require_int(command.get("RegAddress"), "RegAddress", 1, 65536)
    if write_type == "float32_cdab" and address == 65536:
        raise ValueError("float32 write exceeds register address range")
    if command.get("RegLen") is not None:
        expected_length = 1 if write_type == "uint16" else 2
        actual_length = require_int(command.get("RegLen"), "RegLen", 1, 255)
        if actual_length != expected_length:
            raise ValueError(
                f"RegLen={actual_length} conflicts with {source_type}; "
                f"expected {expected_length}"
            )
    write_override = normalize_address_override(
        command.get("RegAddressVersionJudgment"), "RegAddressVersionJudgment"
    )
    result_override = normalize_address_override(
        command.get("ResultRegAddressVersionJudgment"),
        "ResultRegAddressVersionJudgment",
    )
    override_conditions = [
        item[0] for item in (write_override, result_override) if item is not None
    ]
    if len(override_conditions) == 2 and override_conditions[0] != override_conditions[1]:
        raise ValueError("write/result address overrides use different conditions")
    override_condition = override_conditions[0] if override_conditions else None

    editable = command.get("Editable") is True
    normalized_input: dict[str, Any] | None = None
    if editable:
        input_id = f"input_{input_index + 1}"
        minimum, minimum_inclusive = normalize_bound(
            command.get("SetValueMin"), "SetValueMin", "minimum"
        )
        maximum, maximum_inclusive = normalize_bound(
            command.get("SetValueMax"), "SetValueMax", "maximum"
        )
        default = require_number(command.get("SetValue"), "SetValue")
        minimum_ok = default >= minimum if minimum_inclusive else default > minimum
        maximum_ok = default <= maximum if maximum_inclusive else default < maximum
        if minimum > maximum or not minimum_ok or not maximum_ok:
            raise ValueError("input default is outside its normalized bounds")
        if input_type == "uint16":
            for label, value in (
                ("minimum", minimum),
                ("maximum", maximum),
                ("default", default),
            ):
                if int(value) != value or not 0 <= int(value) <= 65535:
                    raise ValueError(f"uint16 input {label} is invalid")
        normalized_input = {
            "id": input_id,
            "label": str(command.get("Name") or calibration.get("Name") or input_id),
            "type": input_type,
            "default": default,
            "minimum": minimum,
            "maximum": maximum,
            "minimum_inclusive": minimum_inclusive,
            "maximum_inclusive": maximum_inclusive,
            "decimals": infer_decimals(reading, input_type),
            "unit": normalize_unit(reading, calibration, command),
            "write_scale": normalize_scale(command.get("Conversion")),
        }
        value = {"kind": "input", "input_id": input_id}
    else:
        if command.get("Conversion") not in (None, ""):
            raise ValueError("Conversion on a fixed-value command is unsupported")
        fixed = require_number(command.get("SetValue"), "SetValue")
        if write_type == "uint16" and (int(fixed) != fixed or not 0 <= int(fixed) <= 65535):
            raise ValueError("fixed uint16 SetValue must be an integer in 0..65535")
        value = {"kind": "fixed", "number": fixed}

    if "Waiting_mS" not in command:
        wait_ms = 0
        ctx.add(
            "review_required",
            file_name,
            product_num,
            path,
            "Waiting_mS is missing; normalized to 0 ms",
        )
    else:
        wait_ms = require_int(command.get("Waiting_mS"), "Waiting_mS", 0, 300_000)

    command_condition = parse_condition(command.get("VersionJudgment"))
    result = normalize_result(command, ctx, file_name, product_num, path)
    alternative = {
        "when": None,
        "write": {"address": address, "type": write_type, "value": value},
        "wait_after_ms": wait_ms,
        "result": result,
    }
    alternatives = [alternative]
    if override_condition is not None:
        alternative["when"] = {"not": override_condition}
        override_alternative = copy.deepcopy(alternative)
        override_alternative["when"] = override_condition
        if write_override is not None:
            override_alternative["write"]["address"] = write_override[1]
        if result_override is not None:
            if override_alternative["result"] is None:
                raise ValueError("result address override requires a non-zero base result")
            override_alternative["result"]["address"] = result_override[1]
        alternatives.append(override_alternative)

    step = {
        "id": f"step_{path.rsplit('[', 1)[-1].rstrip(']')}",
        "when": command_condition,
        "alternatives": alternatives,
    }
    return step, normalized_input, command_condition is not None or override_condition is not None


def normalize_profile(
    source: dict[str, Any], file_name: str, ctx: ImportContext
) -> dict[str, Any] | None:
    product_num = require_int(source.get("PN"), "PN", 1, 0xFFFFFFFF)
    model_name = str(source.get("ModelName") or "").strip()
    if not model_name:
        raise ValueError("ModelName cannot be empty")

    stem = Path(file_name).stem
    suffix = stem.split("_", 1)[1] if "_" in stem else "default"
    variant = safe_id(suffix, "default")
    profile_id = f"{product_num}.{variant}"
    procedures: list[dict[str, Any]] = []
    command_count = 0
    requires_version_read = False

    readings = source.get("Readings") or []
    if not isinstance(readings, list):
        raise ValueError("Readings must be an array")
    for reading_index, reading in enumerate(readings):
        if not isinstance(reading, dict):
            raise ValueError(f"Readings[{reading_index}] must be an object")
        reading_condition = parse_condition(reading.get("VersionJudgment"))
        calibrations = reading.get("Calibrations") or []
        if not isinstance(calibrations, list):
            raise ValueError(f"Readings[{reading_index}].Calibrations must be an array")
        for calibration_index, calibration in enumerate(calibrations):
            if not isinstance(calibration, dict):
                raise ValueError("Calibration must be an object")
            base_path = f"Readings[{reading_index}].Calibrations[{calibration_index}]"
            calibration_condition = parse_condition(calibration.get("VersionJudgment"))
            procedure_condition = merge_conditions(reading_condition, calibration_condition)
            requires_version_read |= procedure_condition is not None
            commands = calibration.get("Cmds") or []
            if not isinstance(commands, list) or not commands:
                raise ValueError(f"{base_path}.Cmds must be a non-empty array")
            if len(commands) > MAX_STEPS_PER_PROCEDURE:
                raise ValueError(f"{base_path} exceeds {MAX_STEPS_PER_PROCEDURE} steps")

            inputs: list[dict[str, Any]] = []
            steps: list[dict[str, Any]] = []
            for command_index, command in enumerate(commands):
                if not isinstance(command, dict):
                    raise ValueError(f"{base_path}.Cmds[{command_index}] must be an object")
                path = f"{base_path}.Cmds[{command_index}]"
                step, normalized_input, command_has_condition = normalize_command(
                    reading,
                    calibration,
                    command,
                    len(inputs),
                    ctx,
                    file_name,
                    product_num,
                    path,
                )
                requires_version_read |= command_has_condition
                steps.append(step)
                if normalized_input is not None:
                    inputs.append(normalized_input)
            if len(inputs) > MAX_INPUTS_PER_PROCEDURE:
                raise ValueError(
                    f"{base_path} has {len(inputs)} inputs; max is {MAX_INPUTS_PER_PROCEDURE}"
                )
            command_count += len(steps)
            reading_name = str(reading.get("Name") or f"Reading {reading_index + 1}")
            calibration_name = str(
                calibration.get("Name") or f"Calibration {calibration_index + 1}"
            )
            procedures.append(
                {
                    "id": f"reading_{reading_index + 1}.calibration_{calibration_index + 1}",
                    "category": "sensor_calibration",
                    "reading_name": reading_name,
                    "name": calibration_name,
                    "message": str(calibration.get("Message") or ""),
                    "unit": str(calibration.get("Unit") or reading.get("Unit") or ""),
                    "when": procedure_condition,
                    "inputs": inputs,
                    "steps": steps,
                }
            )

    raw_spans = source.get("Span4_20mA") or []
    if isinstance(raw_spans, dict):
        raw_spans = [raw_spans]
    if not isinstance(raw_spans, list):
        raise ValueError("Span4_20mA must be an object or array")
    for span_index, span in enumerate(raw_spans):
        if not isinstance(span, dict):
            raise ValueError(f"Span4_20mA[{span_index}] must be an object")
        span_name = str(span.get("Name") or f"4-20mA Output {span_index + 1}")
        for endpoint_key in ("4mA", "20mA"):
            endpoint = span.get(endpoint_key)
            if endpoint is None:
                continue
            if not isinstance(endpoint, dict):
                raise ValueError(
                    f"Span4_20mA[{span_index}].{endpoint_key} must be an object"
                )
            commands = endpoint.get("Cmds") or []
            if not commands:
                continue
            base_path = f"Span4_20mA[{span_index}].{endpoint_key}"
            if not isinstance(commands, list):
                raise ValueError(f"{base_path}.Cmds must be an array")
            if len(commands) > MAX_STEPS_PER_PROCEDURE:
                raise ValueError(f"{base_path} exceeds {MAX_STEPS_PER_PROCEDURE} steps")

            editable_indexes = [
                index
                for index, command in enumerate(commands)
                if isinstance(command, dict)
                and command.get("SetValueMin") is not None
                and command.get("SetValueMax") is not None
            ]
            if endpoint.get("Editable") is True and len(editable_indexes) != 1:
                raise ValueError(
                    f"{base_path} must identify exactly one bounded output-span value"
                )

            inputs: list[dict[str, Any]] = []
            steps: list[dict[str, Any]] = []
            for command_index, raw_command in enumerate(commands):
                if not isinstance(raw_command, dict):
                    raise ValueError(f"{base_path}.Cmds[{command_index}] must be an object")
                command = copy.deepcopy(raw_command)
                if endpoint.get("Editable") is True and command_index == editable_indexes[0]:
                    # Span JSON places editability on the 4mA/20mA endpoint,
                    # while the bounded command itself is often marked false.
                    command["Editable"] = True
                    command.setdefault("Name", endpoint.get("Name"))
                path = f"{base_path}.Cmds[{command_index}]"
                step, normalized_input, command_has_condition = normalize_command(
                    span,
                    endpoint,
                    command,
                    len(inputs),
                    ctx,
                    file_name,
                    product_num,
                    path,
                )
                requires_version_read |= command_has_condition
                steps.append(step)
                if normalized_input is not None:
                    inputs.append(normalized_input)
            if len(inputs) > MAX_INPUTS_PER_PROCEDURE:
                raise ValueError(
                    f"{base_path} has {len(inputs)} inputs; max is {MAX_INPUTS_PER_PROCEDURE}"
                )
            command_count += len(steps)
            procedures.append(
                {
                    "id": f"output_span_{span_index + 1}.{endpoint_key.lower()}",
                    "category": "output_span",
                    "reading_name": span_name,
                    "name": str(endpoint.get("Name") or endpoint_key),
                    "message": str(endpoint.get("Message") or "Set analog output range"),
                    "unit": str(span.get("Unit") or ""),
                    "when": parse_condition(endpoint.get("VersionJudgment")),
                    "inputs": inputs,
                    "steps": steps,
                }
            )

    if not procedures:
        return None
    if len(procedures) > MAX_PROCEDURES:
        raise ValueError(f"procedure count {len(procedures)} exceeds {MAX_PROCEDURES}")
    if command_count > MAX_COMMANDS_PER_PROFILE:
        raise ValueError(f"command count {command_count} exceeds {MAX_COMMANDS_PER_PROFILE}")

    has_inputs = any(procedure["inputs"] for procedure in procedures)
    has_results = any(
        alternative["result"] is not None
        for procedure in procedures
        for step in procedure["steps"]
        for alternative in step["alternatives"]
    )
    if has_inputs:
        ctx.add(
            "review_required",
            file_name,
            product_num,
            "procedures[].inputs",
            "input ranges and value types require review before release",
        )
    if requires_version_read:
        ctx.add(
            "review_required",
            file_name,
            product_num,
            "requires_version_read",
            "version conditions require review and firmware support before release",
        )
    ctx.add(
        "review_required",
        file_name,
        product_num,
        "procedures[].steps",
        "command addresses, values, and waits require review before release",
    )

    own_issues = [issue for issue in ctx.issues if issue.file == file_name]
    release_status = "approved"
    if any(issue.severity in ("error", "unsupported") for issue in own_issues):
        release_status = "blocked"
    elif any(issue.severity == "review_required" for issue in own_issues):
        release_status = "draft"
    return {
        "schema_version": SCHEMA_VERSION,
        "profile_id": profile_id,
        "product_num": product_num,
        "variant": variant,
        "model_name": model_name,
        "source_file": file_name,
        "requires_version_read": requires_version_read,
        "release_status": release_status,
        "review": {
            "input_ranges": not has_inputs,
            "result_mappings": not has_results,
            "version_conditions": not requires_version_read,
            "command_sequences": False,
            "confirmed_operations": [],
        },
        "audit_issues": [
            {
                "severity": issue.severity,
                "path": issue.path,
                "message": issue.message,
            }
            for issue in own_issues
        ],
        "procedures": procedures,
    }


def rules_by_key(rules: dict[str, Any]) -> tuple[set[str], set[int], dict[int, str]]:
    excluded_files = {str(item["file"]) for item in rules.get("exclude_files", [])}
    excluded_pns = {int(item["product_num"]) for item in rules.get("exclude_pns", [])}
    preferred = {
        int(item["product_num"]): str(item["file"])
        for item in rules.get("preferred_files", [])
    }
    return excluded_files, excluded_pns, preferred


def render_markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "# Calibration Import Report",
        "",
        f"- Source files: {summary['source_files']}",
        f"- Parsed files: {summary['parsed_files']}",
        f"- Profiles with calibration: {summary['calibration_profiles']}",
        f"- Generated profiles: {summary['generated_profiles']}",
        f"- Approved: {summary['approved_profiles']}",
        f"- Draft: {summary['draft_profiles']}",
        f"- Blocked: {summary['blocked_profiles']}",
        "",
        "## Issues",
        "",
        "| Severity | File | PN | Path | Message |",
        "|---|---|---:|---|---|",
    ]
    for issue in report["issues"]:
        message = str(issue["message"]).replace("|", "\\|")
        path = str(issue["path"]).replace("|", "\\|")
        lines.append(
            f"| {issue['severity']} | {issue['file']} | "
            f"{issue['product_num'] or ''} | {path} | {message} |"
        )
    return "\n".join(lines) + "\n"


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rules", default="config/calibration_import_rules.json", type=Path
    )
    parser.add_argument("--source", type=Path, help="override source directory")
    parser.add_argument(
        "--source-file", type=Path,
        help="import exactly one mobile-app JSON file from any folder",
    )
    parser.add_argument(
        "--output", default="config/calibration_profiles", type=Path
    )
    parser.add_argument(
        "--report-base", default="reports/calibration_import_report", type=Path
    )
    parser.add_argument(
        "--product-num", action="append", type=int, help="only generate selected PN"
    )
    parser.add_argument(
        "--pilot", action="store_true", help="generate rules file pilot_pns"
    )
    parser.add_argument("--write", action="store_true", help="write normalized profiles")
    parser.add_argument(
        "--overwrite", action="store_true",
        help="replace an existing normalized profile in single-file mode",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str] = ()) -> int:
    args = parse_args(argv)
    rules = load_json(args.rules)
    if rules.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"rules schema_version must be {SCHEMA_VERSION}")
    source_dir = args.source or Path(str(rules.get("source_directory") or ""))
    if args.source_file is not None:
        source_file = args.source_file.resolve()
        if not source_file.is_file():
            raise FileNotFoundError(f"source file not found: {source_file}")
        source_files = [source_file]
        source_dir = source_file.parent
    elif not source_dir.is_dir():
        raise FileNotFoundError(f"source directory not found: {source_dir}")
    else:
        source_files = sorted(
            source_dir.glob("*.json"), key=lambda path: path.name.lower()
        )

    selected_pns: set[int] | None = None
    if args.product_num:
        selected_pns = set(args.product_num)
    elif args.pilot:
        selected_pns = {int(value) for value in rules.get("pilot_pns", [])}

    excluded_files, excluded_pns, preferred = rules_by_key(rules)
    ctx = ImportContext()
    parsed: list[tuple[Path, dict[str, Any], int]] = []
    for path in source_files:
        if path.name in excluded_files:
            ctx.add("info", path.name, None, "", "excluded by project rule")
            continue
        try:
            source = load_json(path)
            if not isinstance(source, dict):
                raise ValueError("JSON root must be an object")
            product_num = require_int(source.get("PN"), "PN", 1, 0xFFFFFFFF)
            filename_prefix = path.stem.split("_", 1)[0]
            if filename_prefix.isdigit() and int(filename_prefix) != product_num:
                raise ValueError(
                    f"filename PN {filename_prefix} differs from JSON PN {product_num}"
                )
            if product_num in excluded_pns:
                ctx.add("info", path.name, product_num, "", "PN excluded by project rule")
                continue
            preferred_file = preferred.get(product_num)
            if preferred_file and path.name != preferred_file:
                ctx.add(
                    "info",
                    path.name,
                    product_num,
                    "",
                    f"superseded by preferred file {preferred_file}",
                )
                continue
            parsed.append((path, source, product_num))
        except Exception as exc:  # report all bad source files in one pass
            ctx.add("error", path.name, None, "", str(exc))

    by_pn: dict[int, list[str]] = defaultdict(list)
    for path, _, product_num in parsed:
        by_pn[product_num].append(path.name)
    conflicted_pns = {pn for pn, names in by_pn.items() if len(names) > 1}
    for product_num in sorted(conflicted_pns):
        ctx.add(
            "error",
            ", ".join(by_pn[product_num]),
            product_num,
            "",
            "duplicate PN has no preferred-file rule",
        )

    profiles: list[dict[str, Any]] = []
    calibration_profile_count = 0
    for path, source, product_num in parsed:
        has_calibration = bool(source.get("Span4_20mA")) or any(
            (reading.get("Calibrations") or [])
            for reading in (source.get("Readings") or [])
            if isinstance(reading, dict)
        )
        calibration_profile_count += bool(has_calibration)
        if not has_calibration or product_num in conflicted_pns:
            continue
        if selected_pns is not None and product_num not in selected_pns:
            continue
        try:
            profile = normalize_profile(source, path.name, ctx)
            if profile is not None:
                # normalize_profile computes status after adding its issues.
                own = [issue for issue in ctx.issues if issue.file == path.name]
                if any(i.severity in ("error", "unsupported") for i in own):
                    profile["release_status"] = "blocked"
                elif any(i.severity == "review_required" for i in own):
                    profile["release_status"] = "draft"
                profiles.append(profile)
        except Exception as exc:
            ctx.add("error", path.name, product_num, "", str(exc))

    if selected_pns is not None:
        generated_pns = {profile["product_num"] for profile in profiles}
        for missing in sorted(selected_pns - generated_pns):
            ctx.add("error", "", missing, "", "selected PN did not generate a profile")

    if args.write:
        for profile in profiles:
            output_path = args.output / f"{profile['profile_id']}.json"
            if (args.source_file is not None and output_path.exists()
                    and not args.overwrite):
                raise FileExistsError(
                    f"normalized profile already exists: {output_path}; "
                    "review or remove it before importing again"
                )
            write_json(output_path, profile)

    status_counts = Counter(profile["release_status"] for profile in profiles)
    severity_counts = Counter(issue.severity for issue in ctx.issues)
    report = {
        "schema_version": SCHEMA_VERSION,
        "source_directory": str(source_dir.resolve()),
        "selected_pns": sorted(selected_pns) if selected_pns is not None else None,
        "summary": {
            "source_files": len(source_files),
            "parsed_files": len(parsed),
            "calibration_profiles": calibration_profile_count,
            "generated_profiles": len(profiles),
            "approved_profiles": status_counts["approved"],
            "draft_profiles": status_counts["draft"],
            "blocked_profiles": status_counts["blocked"],
            "issues_by_severity": dict(sorted(severity_counts.items())),
        },
        "generated": [
            {
                "profile_id": profile["profile_id"],
                "product_num": profile["product_num"],
                "source_file": profile["source_file"],
                "release_status": profile["release_status"],
                "requires_version_read": profile["requires_version_read"],
                "procedures": len(profile["procedures"]),
            }
            for profile in profiles
        ],
        "issues": [asdict(issue) for issue in ctx.issues],
    }
    write_json(args.report_base.with_suffix(".json"), report)
    markdown_path = args.report_base.with_suffix(".md")
    markdown = render_markdown(report)
    old_markdown = markdown_path.read_text(encoding="utf-8") if markdown_path.exists() else None
    if old_markdown != markdown:
        markdown_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_path.write_text(markdown, encoding="utf-8", newline="\n")

    print(json.dumps(report["summary"], ensure_ascii=False, indent=2))
    if selected_pns is None:
        has_relevant_error = severity_counts["error"] > 0
    else:
        has_relevant_error = any(
            issue.severity == "error"
            and (issue.product_num in selected_pns or issue.product_num is None)
            for issue in ctx.issues
        )
    return 1 if has_relevant_error else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
