"""ESPLOG1 协议链路测试脚本。

烧录固件后，在设备 TOOLS MENU 中点击 LOG EXPORT 进入导出模式，
然后运行本脚本验证 PING/LIST/STAT/GET/DELETE/CLOSE 全链路：

    python tools/test_esplog1.py COM21
    python tools/test_esplog1.py COM21 --download D:\\sensor_logs
    python tools/test_esplog1.py COM21 --stat /logs/PN53607_S000001.csv
    python tools/test_esplog1.py COM21 --delete /logs/PN53607_S000001.csv

串口处理参考 upload_sensor_profiles.py：禁止 DTR/RTS 跳变，
避免 ESP32-S3 原生 USB CDC 误触发复位。
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("缺少 pyserial，请执行: python -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


BLOCK_SIZE = 4096
MAGIC = "ESPLOG1"


def open_port(port: str) -> serial.Serial:
    """打开串口，DTR/RTS 不跳变，避免 ESP32-S3 USB CDC 复位。"""
    conn = serial.Serial()
    conn.port = port
    conn.baudrate = 115200
    conn.timeout = 2.0
    conn.write_timeout = 10
    conn.dsrdtr = False
    conn.rtscts = False
    conn.dtr = False
    conn.rts = False
    conn.open()
    time.sleep(0.1)
    conn.reset_input_buffer()
    return conn


def send_line(conn: serial.Serial, line: str) -> None:
    conn.write((line + "\n").encode("ascii"))
    conn.flush()


def read_line(conn: serial.Serial, timeout: float = 5.0) -> str:
    """读取一行（直到 \\n），超时抛 TimeoutError。"""
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        b = conn.read(1)
        if not b:
            continue
        if b == b"\n":
            # 去掉可能的 \r
            if buf and buf[-1:] == b"\r":
                buf = buf[:-1]
            return buf.decode("ascii", errors="replace")
        buf.extend(b)
    raise TimeoutError(f"读行超时，已收 {len(buf)} 字节: {bytes(buf)!r}")


def cmd_ping(conn: serial.Serial) -> dict:
    send_line(conn, "PING")
    line = read_line(conn)
    # PONG ESPLOG1 <cfg> <fw>
    parts = line.split()
    if len(parts) < 4 or parts[0] != "PONG" or parts[1] != MAGIC:
        raise RuntimeError(f"PING 响应异常: {line}")
    return {"magic": parts[1], "config": parts[2], "firmware": parts[3]}


def cmd_list(conn: serial.Serial) -> list[dict]:
    send_line(conn, "LIST")
    count_line = read_line(conn)
    if not count_line.startswith("COUNT "):
        raise RuntimeError(f"LIST 响应异常: {count_line}")
    count = int(count_line.split()[1])
    entries = []
    for _ in range(count):
        line = read_line(conn)
        # path\tsize\tsession\tpn
        fields = line.split("\t")
        if len(fields) < 4:
            raise RuntimeError(f"LIST 条目格式错: {line}")
        entries.append({
            "path": fields[0],
            "size": int(fields[1]),
            "session": int(fields[2]),
            "pn": int(fields[3]),
        })
    end_line = read_line(conn)
    if end_line != "END":
        raise RuntimeError(f"LIST 未以 END 结束: {end_line}")
    return entries


def cmd_stat(conn: serial.Serial, path: str) -> tuple[int, int]:
    send_line(conn, f"STAT {path}")
    line = read_line(conn, timeout=30.0)  # 大文件 CRC32 计算较慢
    if line.startswith("OK "):
        parts = line.split()
        size = int(parts[1])
        crc = int(parts[2], 16)
        return size, crc
    raise RuntimeError(f"STAT 失败: {line}")


def cmd_get_block(conn: serial.Serial, path: str, offset: int,
                  length: int) -> tuple[bytes, bool]:
    """下载一个块。返回 (data, eof)。

    设备响应格式：
        DATA <len>\r\n
        <len 字节二进制>
        CRC <hex>\n
    然后 PC 发 ACK/RESEND/CANCEL。
    如果 offset >= size，设备直接返回 EOF\n（不发 DATA）。
    """
    send_line(conn, f"GET {path} {offset} {length}")
    # 先读一行：可能是 "DATA <len>" 或 "EOF" 或 "ERR ..."
    first = read_line(conn)
    if first == "EOF":
        return b"", True
    if first.startswith("ERR"):
        raise RuntimeError(f"GET 失败: {first}")

    # DATA <len>\r\n —— read_line 已处理 \r\n
    if not first.startswith("DATA "):
        raise RuntimeError(f"GET 响应异常: {first}")
    expected_len = int(first.split()[1])

    # 读取 expected_len 字节二进制
    data = bytearray()
    deadline = time.monotonic() + 10.0
    while len(data) < expected_len and time.monotonic() < deadline:
        chunk = conn.read(expected_len - len(data))
        if chunk:
            data.extend(chunk)
    if len(data) != expected_len:
        raise RuntimeError(
            f"数据块长度不符: 期望 {expected_len}, 实际 {len(data)}")

    # 读 CRC <hex>\n
    crc_line = read_line(conn)
    if not crc_line.startswith("CRC "):
        raise RuntimeError(f"GET CRC 行异常: {crc_line}")
    block_crc = int(crc_line.split()[1], 16)

    # 本地校验块 CRC32
    actual_crc = binascii.crc32(bytes(data)) & 0xFFFFFFFF
    if actual_crc != block_crc:
        # 要求重发
        send_line(conn, "RESEND")
        return cmd_get_block(conn, path, offset, length)

    # 块 CRC 正确，发 ACK
    send_line(conn, "ACK")
    return bytes(data), False


def cmd_download(conn: serial.Serial, path: str, local_path: Path) -> None:
    size, expected_crc = cmd_stat(conn, path)
    print(f"  下载 {path} -> {local_path} ({size} bytes, CRC {expected_crc:08X})")
    local_path.parent.mkdir(parents=True, exist_ok=True)
    offset = 0
    with open(local_path, "wb") as f:
        while offset < size:
            length = min(BLOCK_SIZE, size - offset)
            data, eof = cmd_get_block(conn, path, offset, length)
            if eof:
                break
            f.write(data)
            offset += len(data)
            pct = offset * 100 // size if size else 100
            print(f"\r  {offset}/{size} bytes ({pct}%)", end="", flush=True)
    print()
    # 整文件 CRC 校验
    actual_crc = binascii.crc32(local_path.read_bytes()) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise RuntimeError(
            f"整文件 CRC 不匹配: 期望 {expected_crc:08X}, 实际 {actual_crc:08X}")
    print(f"  CRC32 校验通过: {actual_crc:08X}")


def cmd_delete(conn: serial.Serial, path: str) -> None:
    send_line(conn, f"DELETE {path}")
    line = read_line(conn)
    if not line.startswith("CONFIRM "):
        raise RuntimeError(f"DELETE 失败: {line}")
    token = line.split()[1]
    print(f"  设备要求确认 token: {token}")
    send_line(conn, f"CONFIRM {token}")
    result = read_line(conn)
    if not result.startswith("OK"):
        raise RuntimeError(f"DELETE 确认失败: {result}")
    print(f"  已删除: {path}")


def cmd_close(conn: serial.Serial) -> None:
    send_line(conn, "CLOSE")
    line = read_line(conn)
    if line != "BYE":
        raise RuntimeError(f"CLOSE 响应异常: {line}")


def main() -> int:
    parser = argparse.ArgumentParser(description="ESPLOG1 协议链路测试")
    parser.add_argument("port", help="USB CDC 端口，例如 COM21")
    parser.add_argument("--download", metavar="DIR",
                        help="下载所有日志到指定目录")
    parser.add_argument("--stat", metavar="PATH",
                        help="仅查询指定文件的大小和 CRC32")
    parser.add_argument("--delete", metavar="PATH",
                        help="删除指定文件（需二次确认）")
    parser.add_argument("--skip-close", action="store_true",
                        help="不在最后发送 CLOSE（调试用）")
    args = parser.parse_args()

    conn = open_port(args.port)
    try:
        # 1. PING
        print("[PING]")
        info = cmd_ping(conn)
        print(f"  magic={info['magic']} config={info['config']} "
              f"firmware={info['firmware']}")

        # 2. LIST
        print("[LIST]")
        entries = cmd_list(conn)
        if not entries:
            print("  (无日志文件)")
        for e in entries:
            print(f"  {e['path']}\t{e['size']}B\tS{e['session']}\tPN{e['pn']}")

        # 3. STAT（指定文件或第一个文件）
        target = args.stat
        if target is None and entries:
            target = entries[0]["path"]
        if target:
            print(f"[STAT] {target}")
            size, crc = cmd_stat(conn, target)
            print(f"  size={size} crc32={crc:08X}")

        # 4. DOWNLOAD
        if args.download:
            print(f"[DOWNLOAD] 保存到 {args.download}")
            for e in entries:
                local = Path(args.download) / Path(e["path"]).name
                cmd_download(conn, e["path"], local)

        # 5. DELETE
        if args.delete:
            print(f"[DELETE] {args.delete}")
            cmd_delete(conn, args.delete)

        # 6. CLOSE
        if not args.skip_close:
            print("[CLOSE]")
            cmd_close(conn)
            print("  设备已退出 LOG EXPORT 模式")

    except (RuntimeError, TimeoutError, OSError) as e:
        print(f"测试失败: {e}", file=sys.stderr)
        return 1
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
