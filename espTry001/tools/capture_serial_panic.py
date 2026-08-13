import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture serial output for panic diagnosis.")
    parser.add_argument("port")
    parser.add_argument("--seconds", type=float, default=90.0)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as stream:
        deadline = time.monotonic() + args.seconds
        print(f"LISTENING {args.port}", flush=True)
        while time.monotonic() < deadline:
            data = stream.read(stream.in_waiting or 1)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    print("\nLISTEN_DONE", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
