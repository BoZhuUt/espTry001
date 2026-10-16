#!/usr/bin/env python3
"""通过 ESPTIME1 将电脑 UTC 时间写入设备 RTC。

使用前在设备上进入 SYSTEM INFO > TOOLS MENU > TIME SYNC，然后运行：

    python tools/sync_rtc.py COM12

Firmware does not require DTR/RTS; automatic reboot is disabled.
"""

from __future__ import annotations

import argparse
import getpass
import time

import serial


def read_line(port: serial.Serial, timeout: float = 3.0) -> str:
    """读取一个 ASCII 协议行。

    使用显式总超时而不是永久等待；超时异常会携带已经收到的内容，
    便于区分设备无响应与只收到半行的 USB CDC 问题。
    """
    deadline = time.monotonic() + timeout
    received = bytearray()
    while time.monotonic() < deadline:
        value = port.read(1)
        if not value:
            continue
        if value == b"\n":
            line = received.rstrip(b"\r").decode("ascii", errors="replace")
            # Ignore ESP-IDF/Arduino runtime diagnostics sharing USB CDC.
            # They use forms such as: [1499723][E][STA.cpp:540] ...
            if line.startswith("[") and "][" in line:
                received.clear()
                continue
            return line
        received.extend(value)
    partial = received.decode("ascii", errors="replace")
    raise TimeoutError(f"读行超时，已收 {len(received)} 字节: {partial!r}")


def command(port: serial.Serial, text: str, timeout: float = 3.0) -> str:
    """发送一条 ESPTIME1 命令并返回对应的单行响应。"""
    port.write((text + "\n").encode("ascii"))
    port.flush()
    return read_line(port, timeout)


def expect_prefix(response: str, prefix: str) -> None:
    """校验响应前缀，并把设备 ERR 转换成清晰的脚本异常。"""
    if response.startswith("ERR "):
        raise RuntimeError(f"设备拒绝请求: {response}")
    if not response.startswith(prefix):
        raise RuntimeError(f"响应异常，期望 {prefix!r}: {response!r}")


def handshake(port: serial.Serial, attempts: int = 4) -> str:
    """与 ESPTIME1 建立握手，并容忍 USB CDC 打开后的首包丢失。

    ESP32-S3 原生 USB 在主机刚打开 COM 口时，第一包可能早于 CDC 链路
    完全稳定。每次失败都会清掉迟到的残留响应后重发 PING；全部失败时
    给出包含操作检查项的错误，而不是直接暴露低层读行超时。
    """
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            response = command(port, "PING", timeout=1.5)
            expect_prefix(response, "PONG ESPTIME1 ")
            return response
        except (TimeoutError, RuntimeError) as error:
            last_error = error
            print(f"Handshake attempt {attempt}/{attempts} failed: {error}")
            port.reset_input_buffer()
            time.sleep(0.35)

    raise TimeoutError(
        "ESPTIME1 握手失败；请确认已烧录本次新固件、屏幕停留在 "
        "TIME SYNC 页面，并且所选 COM 口属于该设备"
    ) from last_error


def close_session(port: serial.Serial) -> None:
    """发送 CLOSE 并确认设备安全返回 PASS。"""
    bye = command(port, "CLOSE")
    expect_prefix(bye, "BYE")


def encode_wifi_field(value: str, maximum_bytes: int, label: str,
                      allow_empty: bool = False) -> str:
    """将 Wi-Fi 文本编码为 ESPTIME1 的无空格十六进制 token。

    限制按 UTF-8 字节而非 Python 字符计算，与 ESP32 Wi-Fi
    驱动和设备端固定缓冲区的实际容量保持一致。
    """
    encoded = value.encode("utf-8")
    if not encoded and not allow_empty:
        raise ValueError(f"{label} 不能为空")
    if len(encoded) > maximum_bytes:
        raise ValueError(f"{label} 超过 {maximum_bytes} 字节")
    return encoded.hex() if encoded else "-"


def wait_for_ntp(port: serial.Serial, timeout: float = 150.0) -> str:
    """轮询 Wi-Fi 状态，允许固件完成多轮自动退避重试。"""
    deadline = time.monotonic() + timeout
    previous = ""
    while time.monotonic() < deadline:
        response = command(port, "WIFI STATUS")
        expect_prefix(response, "WIFI ")
        if response != previous:
            print(f"NTP: {response}")
            previous = response
        if " SYNCED" in response:
            return response
        if any(marker in response for marker in
               ("FAIL", "NOT_CONFIGURED", "NO WIFI CONFIG")):
            raise RuntimeError(f"NTP 校时失败: {response}")
        time.sleep(0.5)
    raise TimeoutError("等待 NTP 完成超时")


def main() -> int:
    """执行握手、写入、回读校验和安全退出的完整校时流程。"""
    parser = argparse.ArgumentParser(description="Sync ESP32 RTC via ESPTIME1")
    parser.add_argument("port", help="USB CDC port, for example COM12")
    parser.add_argument("--baud", type=int, default=115200)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--ssid", help="保存 Wi-Fi SSID 并立即测试 NTP")
    actions.add_argument("--wifi-status", action="store_true",
                         help="查询脱敏 Wi-Fi/NTP 状态")
    actions.add_argument("--clear-wifi", action="store_true",
                         help="清除 NVS 中的 Wi-Fi 凭据")
    actions.add_argument("--ntp-start", action="store_true",
                         help="使用已保存凭据手动测试 NTP")
    parser.add_argument(
        "--password",
        help="Wi-Fi 密码；省略时安全提示输入，空字符串表示开放网络")
    args = parser.parse_args()
    if args.password is not None and args.ssid is None:
        parser.error("--password 必须与 --ssid 一起使用")

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.05
    port.write_timeout = 3.0
    port.dsrdtr = False
    port.rtscts = False
    port.dtr = False
    port.rts = False
    port.open()

    try:
        time.sleep(0.2)
        port.reset_input_buffer()
        print(f"Open {args.port} with DTR/RTS disabled")

        pong = handshake(port)
        print(f"Handshake: {pong}")

        if args.wifi_status:
            status = command(port, "WIFI STATUS")
            expect_prefix(status, "WIFI ")
            print(status)
            close_session(port)
            print("RESULT: PASS")
            return 0

        if args.clear_wifi:
            cleared = command(port, "WIFI CLEAR")
            expect_prefix(cleared, "OK WIFI CLEARED")
            print(cleared)
            close_session(port)
            print("RESULT: PASS")
            return 0

        if args.ssid is not None:
            password = (args.password if args.password is not None
                        else getpass.getpass("WiFi password (empty = open): "))
            ssid_token = encode_wifi_field(args.ssid, 32, "SSID")
            password_token = encode_wifi_field(
                password, 63, "Password", allow_empty=True)
            stored = command(
                port, f"WIFI SET {ssid_token} {password_token}", timeout=5.0)
            expect_prefix(stored, "OK WIFI STORED")
            print("WiFi credentials stored (password hidden)")
            started = command(port, "NTP START", timeout=5.0)
            expect_prefix(started, "OK NTP STARTED")
            wait_for_ntp(port)
            after = command(port, "GET")
            expect_prefix(after, "TIME ")
            print(f"After NTP: {after}")
            close_session(port)
            print("RESULT: PASS")
            return 0

        if args.ntp_start:
            started = command(port, "NTP START", timeout=5.0)
            expect_prefix(started, "OK NTP STARTED")
            wait_for_ntp(port)
            after = command(port, "GET")
            expect_prefix(after, "TIME ")
            print(f"After NTP: {after}")
            close_session(port)
            print("RESULT: PASS")
            return 0

        before = command(port, "GET")
        if before.startswith("TIME "):
            print(f"Before: {before}")
        else:
            print(f"Before: {before} (expected when RTC is not set)")

        # 在真正发送前尽可能晚地取整，缩短电脑取时到 RTC 写入的延迟。
        host_utc = int(time.time())
        updated = command(port, f"SET {host_utc}", timeout=5.0)
        expect_prefix(updated, "OK ")
        print(f"Set: {updated}")

        after = command(port, "GET")
        expect_prefix(after, "TIME ")
        parts = after.split(" ", 2)
        device_utc = int(parts[1])
        error_seconds = device_utc - int(time.time())
        print(f"After: {after}")
        print(f"Difference from PC: {error_seconds:+d} second(s)")
        if abs(error_seconds) > 1:
            raise RuntimeError("回读时间与电脑 UTC 相差超过 1 秒")

        close_session(port)
        print("RESULT: PASS")
        return 0
    finally:
        port.close()


if __name__ == "__main__":
    raise SystemExit(main())
