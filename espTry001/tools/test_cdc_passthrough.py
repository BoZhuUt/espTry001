"""TinyUSB hardware acceptance. Board must be in ordinary PASS.
Run: python tools/test_cdc_passthrough.py COMxx COM6 --report reports/cdc.json
Uses read-only Modbus requests; no device configuration is written.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import time
import serial

FORMATS = [(b, p) for b in (9600, 19200, 115200) for p in ('N', 'E', 'O')]
PARITY = {'N': 0, 'O': 1, 'E': 2}

def frame(data):
    crc = 0xffff
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return data + struct.pack('<H', crc)

def open_port(name, baud=9600, parity='E', dtr=False):
    port = serial.Serial()
    port.port, port.baudrate, port.parity = name, baud, parity
    port.timeout, port.write_timeout = .05, 3
    port.dtr, port.rts = dtr, False
    port.open()
    return port

def read_exact(port, size, timeout=2):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size and time.monotonic() < deadline:
        data.extend(port.read(size - len(data)))
    return bytes(data)

def registers(port, start, count):
    port.reset_input_buffer()
    port.write(frame(struct.pack('>BBHH', 247, 3, start, count)))
    port.flush()
    data = read_exact(port, 5 + count * 2)
    assert len(data) == 5 + count * 2, f'Diagnostic length: {data.hex()}'
    assert data[:3] == bytes((247, 3, count * 2)), f'Diagnostic header: {data.hex()}'
    assert frame(data[:-2]) == data, 'Diagnostic CRC'
    return list(struct.unpack(f'>{count}H', data[3:-2]))

def diagnostics(port):
    d = registers(port, 43, 8)
    return dict(baud=d[0] << 16 | d[1], parity=d[2], supported=bool(d[3]),
                revision=d[4] << 16 | d[5], reconfigurations=d[6] << 16 | d[7])

def await_settings(port, baud, parity, supported=True):
    deadline = time.monotonic() + 5
    last = None
    while time.monotonic() < deadline:
        try:
            last = diagnostics(port)
            if last['supported'] == supported and (not supported or
                (last['baud'] == baud and last['parity'] == PARITY[parity])):
                return last
        except AssertionError as error:
            last = str(error)
        time.sleep(.1)
    raise AssertionError(f'Settings not applied {baud}/8{parity}1: {last}')

def verify_pair(esp, peer, count, rng):
    for i in range(count):
        esp.reset_input_buffer()
        peer.reset_input_buffer()
        request = frame(struct.pack('>BBHH', 17, 3, 0x1000 + i, 1))
        esp.write(request)
        esp.flush()
        received = read_exact(peer, len(request))
        assert received == request, f'USB->RS485 mismatch: {received.hex()}'
        payload = bytes(rng.randrange(256) for _ in range(240))
        response = frame(bytes((17, 3, len(payload))) + payload)
        peer.write(response)
        peer.flush()
        received = read_exact(esp, len(response))
        assert received == response, f'RS485->USB mismatch: {len(received)}/{len(response)}'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('esp_port')
    parser.add_argument('rs485_port')
    parser.add_argument('--count', type=int, default=5)
    parser.add_argument('--dtr', choices=('off', 'on'), default='off')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.esp_port.lower() == args.rs485_port.lower() or args.count < 1:
        parser.error('Use two different ports and a positive --count')
    report = dict(esp_port=args.esp_port, rs485_port=args.rs485_port,
                  started=time.strftime('%Y-%m-%dT%H:%M:%S%z'), checks=[])
    report.update(dtr=args.dtr, repetitions=args.count)
    host_dtr = args.dtr == 'on'
    rng = random.Random(20261016)
    try:
        with open_port(args.rs485_port) as peer:
            with open_port(args.esp_port, dtr=host_dtr) as esp:
                time.sleep(.4)
                modes = registers(esp, 7, 2)
                assert modes == [0, 1], f'Board must be in ordinary PASS: modes={modes}'
                up = registers(esp, 40, 2)
                initial_uptime = up[0] << 16 | up[1]
                initial_host_time = time.monotonic()
                for baud, parity in FORMATS + list(reversed(FORMATS)):
                    peer.baudrate, peer.parity = baud, parity
                    esp.baudrate, esp.parity = baud, parity
                    time.sleep(.1)
                    status = await_settings(esp, baud, parity)
                    verify_pair(esp, peer, args.count, rng)
                    report['checks'].append(dict(kind='live', baud=baud, parity=parity, status=status))
                    print(f'PASS live {baud}/8{parity}1 x{args.count}', flush=True)
                for changes in ({'baudrate': 38400}, {'baudrate': 1200}, {'bytesize': 7}, {'stopbits': 2}):
                    esp.apply_settings(dict(baudrate=9600, parity='E', bytesize=8, stopbits=1))
                    esp.apply_settings(changes)
                    time.sleep(.15)
                    await_settings(esp, 9600, 'E', False)
                    peer.reset_input_buffer()
                    esp.write(frame(struct.pack('>BBHH', 17, 3, 0x1000, 1)))
                    esp.flush()
                    assert not read_exact(peer, 1, .3), f'Unsupported settings forwarded: {changes}'
                    report['checks'].append(dict(kind='rejected', settings=changes))
                    print(f'PASS rejected {changes}', flush=True)
                esp.apply_settings(dict(baudrate=9600, parity='E', bytesize=8, stopbits=1))
                peer.baudrate, peer.parity = 9600, 'E'
                time.sleep(.15)
                await_settings(esp, 9600, 'E')
                verify_pair(esp, peer, args.count, rng)
            for baud, parity in FORMATS:
                peer.baudrate, peer.parity = baud, parity
                for attempt in range(2):
                    with open_port(args.esp_port, baud, parity, dtr=host_dtr) as esp:
                        time.sleep(.25)
                        status = await_settings(esp, baud, parity)
                        verify_pair(esp, peer, args.count, rng)
                        report['checks'].append(dict(kind='reopen', baud=baud, parity=parity,
                                                     attempt=attempt, status=status))
                    time.sleep(.1)
                print(f'PASS reopen {baud}/8{parity}1 twice', flush=True)
            with open_port(args.esp_port, 9600, 'E', dtr=host_dtr) as esp:
                peer.baudrate, peer.parity = 9600, 'E'
                time.sleep(.25)
                await_settings(esp, 9600, 'E')
                for dtr, rts in ((False, True), (True, True), (True, False), (False, False), (True, False)):
                    esp.dtr, esp.rts = dtr, rts
                    time.sleep(.05)
                time.sleep(.15)
                await_settings(esp, 9600, 'E')
                verify_pair(esp, peer, args.count, rng)
                up = registers(esp, 40, 2)
                elapsed_ms = (time.monotonic() - initial_host_time) * 1000
                assert (up[0] << 16 | up[1]) - initial_uptime >= elapsed_ms - 1000, 'Unexpected device reboot'
                for dtr, rts in ((False, False), (False, True), (True, False), (True, True)):
                    esp.dtr, esp.rts = dtr, rts
                    time.sleep(.1)
                    verify_pair(esp, peer, args.count, rng)
                    report['checks'].append(dict(kind='control_lines', dtr=dtr, rts=rts))
                report['checks'].append(dict(kind='reset_lines_no_reboot'))
                print('PASS DTR/RTS reset sequence did not reboot', flush=True)
        report['passed'] = True
    except Exception as error:
        report.update(passed=False, error=str(error))
        raise
    finally:
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
