"""Best-effort capture of the short native-USB startup download window.

Does not flash firmware. Requires a full external power/USB disconnect after
ARMED. A normal reboot is not guaranteed to offer a usable window.
"""
import argparse
import subprocess
import time
from pathlib import Path
import serial
from serial.tools import list_ports


def open_touch(port):
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 1200
    connection.timeout = .05
    connection.write_timeout = .1
    connection.dtr = False
    connection.rts = False
    connection.open()
    time.sleep(.01)
    connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-port', required=True)
    parser.add_argument('--rom-port', required=True)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--esptool', type=Path,
                        default=Path.home()/'.platformio/penv/Scripts/esptool.exe')
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout
    print('ARMED: disconnect USB and all power for at least 2 seconds, then reconnect USB.', flush=True)
    while time.monotonic() < deadline:
        if args.app_port not in {p.device for p in list_ports.comports()}:
            break
        time.sleep(.05)
    else:
        print('No disconnect observed. No flash operation performed.', flush=True)
        return 2
    print('Disconnect observed; waiting for startup USB.', flush=True)
    last_scan = 0
    capture_deadline = min(deadline, time.monotonic()+30)
    while time.monotonic() < capture_deadline:
        try:
            open_touch(args.app_port)
        except (serial.SerialException, OSError):
            pass
        if time.monotonic()-last_scan >= .08:
            last_scan = time.monotonic()
            if args.rom_port in {p.device for p in list_ports.comports()}:
                print('Download USB port detected; verifying ROM access.', flush=True)
                command = [str(args.esptool),
                           '--chip','esp32s3','--port',args.rom_port,
                           '--before','usb-reset','--after','no-reset','chip-id']
                try:
                    result = subprocess.run(command, timeout=12, check=False)
                    if result.returncode == 0:
                        print('ROM verified. Left in download mode; no flash written.', flush=True)
                        return 0
                except subprocess.TimeoutExpired:
                    pass
        time.sleep(.002)
    # A missed 1200-baud attempt can leave the old PASS settings unsupported.
    # Restore a supported format, without sending any application payload.
    try:
        connection = serial.Serial()
        connection.port, connection.baudrate, connection.parity = args.app_port, 9600, 'E'
        connection.timeout = .1
        connection.dtr, connection.rts = False, False
        connection.open()
        connection.close()
    except (serial.SerialException, OSError):
        pass
    print('No download window captured. Firmware was not flashed.', flush=True)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
