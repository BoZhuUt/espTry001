"""通过 ESPCFG1 协议上传传感器 Profile JSON。

使用前先在设备 SYSTEM INFO 页面点击 CONFIG UPLOAD：
    python tools/upload_sensor_profiles.py COM21 config/sensor_profiles.json
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError:
    print("缺少 pyserial，请执行: python -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


MAGIC = b"ESPCFG1"
MAX_JSON_SIZE = 64 * 1024
USB_CHUNK_SIZE = 64


def wait_control(connection: serial.Serial, expected: bytes,
                 timeout_seconds: float = 3.0) -> None:
    """等待下位机块级握手字符，避免依靠固定延时猜测接收速度。"""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = connection.read(1)
        if value == expected:
            return
        if value == b"N":
            # 设备可能已经开始返回 NAK，读取剩余一行帮助定位。
            remainder = connection.readline().decode("utf-8", errors="replace").strip()
            raise RuntimeError(f"设备拒绝请求: N{remainder}")
    raise TimeoutError(f"等待设备握手 {expected.decode()} 超时")


def read_payload(path: Path) -> bytes:
    """读取文件并先在 PC 端检查 UTF-8、JSON 语法和长度。"""
    payload = path.read_bytes()
    if not payload or len(payload) > MAX_JSON_SIZE:
        raise ValueError(f"JSON 长度必须在 1~{MAX_JSON_SIZE} 字节")
    text = payload.decode("utf-8")
    json.loads(text)
    return payload


def upload(port: str, payload: bytes) -> str:
    """发送协议头与 JSON，并等待设备返回 ACK 或 NAK。"""
    crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    header = MAGIC + struct.pack("<II", len(payload), crc32)

    # 必须先创建“未打开”的端口对象，再设置 DTR/RTS，最后 open()。
    # Serial(port, ...) 会先用默认 DTR 打开，再修改控制线，ESP32-S3
    # 原生 USB CDC 可能把这个跳变解释为复位，导致 CONFIG 页面丢失。
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 0.2
    connection.write_timeout = 10
    connection.dsrdtr = False
    connection.rtscts = False
    connection.dtr = False
    connection.rts = False
    try:
        connection.open()
        # 清掉进入 CONFIG 页面以前残留的 USB 数据。
        time.sleep(0.1)
        connection.reset_input_buffer()
        connection.write(header)
        connection.flush()
        # 设备创建临时文件后返回 R（Ready），收到前不能发送 Payload。
        wait_control(connection, b"R")
        # 每发一个 USB Full-Speed 数据包就等待 P（Payload accepted）。
        # 这是应用层流控制，不再依赖固定 sleep 避免 CDC 缓冲区溢出。
        for offset in range(0, len(payload), USB_CHUNK_SIZE):
            chunk = payload[offset:offset + USB_CHUNK_SIZE]
            connection.write(chunk)
            connection.flush()
            wait_control(connection, b"P")

        deadline = time.monotonic() + 15
        response = bytearray()
        while time.monotonic() < deadline:
            value = connection.read(1)
            if not value:
                continue
            response.extend(value)
            if value == b"\n":
                break
    finally:
        connection.close()

    if not response:
        raise TimeoutError("设备无响应；请确认屏幕已进入 CONFIG UPLOAD 且 COM 口正确")
    return response.decode("utf-8", errors="replace").strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="上传 ESP32 传感器 Profile JSON")
    parser.add_argument("port", help="USB CDC 端口，例如 COM21")
    parser.add_argument("json_file", type=Path, help="要上传的 JSON 文件")
    args = parser.parse_args()

    try:
        payload = read_payload(args.json_file)
        print(f"上传 {len(payload)} 字节到 {args.port} ...")
        response = upload(args.port, payload)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError, RuntimeError,
            TimeoutError, serial.SerialException) as error:
        print(f"上传失败: {error}", file=sys.stderr)
        return 1

    print(f"设备返回: {response}")
    if response != "ACK":
        return 1
    print("配置安装成功，设备正在重启。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
