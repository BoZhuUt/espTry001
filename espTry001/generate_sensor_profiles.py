"""校验传感器 JSON，并生成固件内置的 SensorProfile C++ 表。

PlatformIO 会在编译前自动调用。也可以手动执行：
    python generate_sensor_profiles.py

脚本只使用 Python 标准库，不需要安装第三方模块。
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


MAX_PROFILES = 32
MAX_FIELDS = 16
MAX_PROFILE_NAME_BYTES = 23
MAX_FIELD_KEY_BYTES = 23
MAX_FIELD_LABEL_BYTES = 23
MAX_CONFIG_VERSION_BYTES = 23
MAX_JSON_FILE_SIZE = 64 * 1024
SCHEMA_VERSION = 1
TYPE_INFO = {
    "uint16": (1, "SensorValueType::UInt16"),
    "float32": (2, "SensorValueType::Float32CDAB"),
}


def require(condition: bool, message: str) -> None:
    """配置不符合当前固件能力时，中止生成并给出原因。"""
    if not condition:
        raise ValueError(message)


def cpp_string(value: str) -> str:
    """把 JSON 字符串转换为安全的 C++ UTF-8 字符串字面量。"""
    return json.dumps(value, ensure_ascii=False)


def utf8_size(value: str) -> int:
    """设备端使用 UTF-8 char 数组，因此限制编码后的字节数而非字符数。"""
    return len(value.encode("utf-8"))


def validate_field(field: dict[str, Any], context: str) -> dict[str, Any]:
    """校验一个字段，并补充寄存器数量和对应的 C++ 枚举。"""
    required = ("key", "label", "function", "address", "address_base",
                "type", "decimals")
    for name in required:
        require(name in field, f"{context}: 缺少字段 {name}")
    require(isinstance(field["key"], str) and field["key"],
            f"{context}: key 必须是非空字符串")
    require(utf8_size(field["key"]) <= MAX_FIELD_KEY_BYTES,
            f"{context}: key 的 UTF-8 长度不能超过 {MAX_FIELD_KEY_BYTES} 字节")
    require(isinstance(field["label"], str) and field["label"],
            f"{context}: label 必须是非空字符串")
    require(utf8_size(field["label"]) <= MAX_FIELD_LABEL_BYTES,
            f"{context}: label 的 UTF-8 长度不能超过 {MAX_FIELD_LABEL_BYTES} 字节")
    require(field["function"] == 3,
            f"{context}: 当前固件只支持功能码 03")
    require(field["address_base"] == 1,
            f"{context}: JSON 必须使用 1-based 地址")
    require(isinstance(field["address"], int) and 1 <= field["address"] <= 65536,
            f"{context}: address 必须在 1~65536")
    require(field["type"] in TYPE_INFO,
            f"{context}: 不支持类型 {field['type']!r}")
    require(isinstance(field["decimals"], int) and 0 <= field["decimals"] <= 3,
            f"{context}: decimals 必须在 0~3")

    register_count, cpp_type = TYPE_INFO[field["type"]]
    if field["type"] == "float32":
        require(field.get("word_order") == "CDAB",
                f"{context}: 当前 float32 只支持 CDAB")
    require(field["address"] + register_count - 1 <= 65536,
            f"{context}: 字段超出 Modbus 地址范围")

    result = dict(field)
    result["register_count"] = register_count
    result["cpp_type"] = cpp_type
    return result


def build_blocks(fields: list[dict[str, Any]], context: str) -> list[dict[str, int]]:
    """按地址排序，并把首尾相接的字段合并成连续 FC03 读取块。"""
    indexed = sorted(enumerate(fields), key=lambda item: item[1]["address"])
    blocks: list[dict[str, int]] = []
    previous_end = 0
    for _, field in indexed:
        start = field["address"]
        end = start + field["register_count"]
        require(start >= previous_end,
                f"{context}: 字段 {field['key']} 与前一个字段地址重叠")
        previous_end = end
        if blocks and start == blocks[-1]["end"]:
            blocks[-1]["end"] = end
        else:
            blocks.append({"start": start, "end": end})
        block_index = len(blocks) - 1
        field["block_index"] = block_index
        field["register_offset"] = start - blocks[block_index]["start"]
    return blocks


def validate_config(data: Any) -> list[dict[str, Any]]:
    """校验整个 JSON，返回已经计算好读取块的 Profile 列表。"""
    require(isinstance(data, dict), "JSON 根节点必须是对象")
    require(data.get("schema_version") == SCHEMA_VERSION,
            f"schema_version 必须为 {SCHEMA_VERSION}")
    require(isinstance(data.get("config_version"), str) and
            data["config_version"], "config_version 必须是非空字符串")
    require(utf8_size(data["config_version"]) <= MAX_CONFIG_VERSION_BYTES,
            f"config_version 不能超过 {MAX_CONFIG_VERSION_BYTES} 字节")
    profiles = data.get("profiles")
    require(isinstance(profiles, list) and profiles, "profiles 必须是非空数组")
    require(len(profiles) <= MAX_PROFILES,
            f"Profile 数量不能超过 {MAX_PROFILES}")

    result: list[dict[str, Any]] = []
    seen_pn: set[int] = set()
    for profile_index, profile in enumerate(profiles):
        context = f"profiles[{profile_index}]"
        require(isinstance(profile, dict), f"{context}: 必须是对象")
        product_num = profile.get("product_num")
        require(isinstance(product_num, int) and 1 <= product_num <= 0xFFFFFFFF,
                f"{context}: product_num 必须是有效 UInt32")
        require(product_num not in seen_pn, f"{context}: PN {product_num} 重复")
        seen_pn.add(product_num)
        require(isinstance(profile.get("name"), str) and profile["name"],
                f"{context}: name 必须是非空字符串")
        require(utf8_size(profile["name"]) <= MAX_PROFILE_NAME_BYTES,
                f"{context}: name 不能超过 {MAX_PROFILE_NAME_BYTES} 字节")
        raw_fields = profile.get("fields")
        require(isinstance(raw_fields, list) and raw_fields,
                f"{context}: fields 必须是非空数组")
        require(len(raw_fields) <= MAX_FIELDS,
                f"{context}: 字段数量不能超过 {MAX_FIELDS}")
        fields = [validate_field(field, f"{context}.fields[{index}]")
                  for index, field in enumerate(raw_fields)]
        keys = [field["key"] for field in fields]
        require(len(keys) == len(set(keys)), f"{context}: key 不能重复")
        blocks = build_blocks(fields, context)
        result.append({"product_num": product_num, "name": profile["name"],
                       "fields": fields, "blocks": blocks})
    return result


def generate_cpp(profiles: list[dict[str, Any]], source_name: str,
                 config_version: str) -> str:
    """把校验后的 Profile 转换为可直接 include 的 C++ 定义。"""
    lines = [
        "// 此文件由 generate_sensor_profiles.py 自动生成，请勿手工修改。",
        f"// 配置来源：{source_name}",
        "",
        f"const char kBuiltInProfileConfigVersion[] = {cpp_string(config_version)};",
        "",
    ]
    for profile in profiles:
        suffix = str(profile["product_num"])
        lines.append(f"const SensorPollBlock kPn{suffix}Blocks[] = {{")
        for block in profile["blocks"]:
            lines.append(f"  {{{block['start']}, {block['end'] - block['start']}}},")
        lines.extend(["};", "", f"const SensorField kPn{suffix}Fields[] = {{"])
        for field in profile["fields"]:
            lines.append(
                f"  {{{cpp_string(field['label'])}, {field['block_index']}, "
                f"{field['register_offset']}, {field['cpp_type']}, "
                f"{field['decimals']}}},")
        lines.extend(["};", ""])

    lines.append("const SensorProfile kProfiles[] = {")
    for profile in profiles:
        suffix = str(profile["product_num"])
        lines.extend([
            "  {",
            f"    {profile['product_num']},",
            f"    {cpp_string(profile['name'])},",
            f"    kPn{suffix}Blocks,",
            f"    sizeof(kPn{suffix}Blocks) / sizeof(kPn{suffix}Blocks[0]),",
            f"    kPn{suffix}Fields,",
            f"    sizeof(kPn{suffix}Fields) / sizeof(kPn{suffix}Fields[0])",
            "  },",
        ])
    lines.extend(["};", ""])
    return "\n".join(lines)


def main(project_dir: Path) -> None:
    """生成文件；内容未变化时不修改时间戳，避免无意义重编译。"""
    # Calibration data shares the LittleFS image with sensor_profiles.json.
    # Build it on every firmware build so an approved profile can never drift
    # from the filesystem input silently. The publisher refuses stale files.
    calibration_builder = project_dir / "tools" / "build_calibration_bundle.py"
    subprocess.run(
        [sys.executable, str(calibration_builder)],
        cwd=project_dir,
        check=True,
    )
    source = project_dir / "config" / "sensor_profiles.json"
    output = project_dir / "src" / "sensor_profiles_generated.inc"
    runtime_json = project_dir / "data" / "sensor_profiles.json"
    with source.open("r", encoding="utf-8") as file:
        data = json.load(file)
    profiles = validate_config(data)
    generated = generate_cpp(profiles, source.relative_to(project_dir).as_posix(),
                             data["config_version"])
    old_content = output.read_text(encoding="utf-8") if output.exists() else None
    if old_content != generated:
        output.write_text(generated, encoding="utf-8", newline="\n")
        print(f"*** Generated {output.name}: {len(profiles)} profile(s) ***")
    else:
        print(f"*** Sensor profiles unchanged: {len(profiles)} profile(s) ***")

    # LittleFS 镜像使用同一份规范化 JSON，避免 config/ 与 data/ 手工维护漂移。
    runtime_content = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    require(len(runtime_content.encode("utf-8")) <= MAX_JSON_FILE_SIZE,
            f"LittleFS JSON 不能超过 {MAX_JSON_FILE_SIZE} 字节")
    runtime_json.parent.mkdir(parents=True, exist_ok=True)
    old_runtime = (runtime_json.read_text(encoding="utf-8")
                   if runtime_json.exists() else None)
    if old_runtime != runtime_content:
        runtime_json.write_text(runtime_content, encoding="utf-8", newline="\n")
        print(f"*** Synced LittleFS file: {runtime_json.name} ***")


try:
    Import("env")  # type: ignore[name-defined]  # 由 PlatformIO/SCons 注入
except NameError:
    env = None

if env is not None:
    main(Path(env.subst("$PROJECT_DIR")))
elif __name__ == "__main__":
    main(Path(__file__).resolve().parent)
