"""Verify host CDC settings cannot reconfigure MASTER/PRECHECK UART.
Select the requested mode on the screen before running this script.
"""
import argparse
import json
import time
from pathlib import Path
from test_cdc_passthrough import FORMATS, open_port, registers, diagnostics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('esp_port')
    parser.add_argument('--mode', choices=('master', 'precheck'), required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    expected = [1, 2] if args.mode == 'master' else [7, 5]
    result = dict(mode=args.mode, checks=[])
    try:
        with open_port(args.esp_port) as esp:
            time.sleep(.3)
            assert registers(esp, 7, 2) == expected, 'Select the requested mode on the screen'
            before = diagnostics(esp)
            assert before['baud'] == 9600 and before['parity'] == 2
            for baud, parity in FORMATS + [(38400, 'N'), (1200, 'N')]:
                esp.baudrate, esp.parity = baud, parity
                time.sleep(.15)
                current = diagnostics(esp)
                assert registers(esp, 7, 2) == expected, 'Operating mode changed'
                assert current['baud'] == 9600 and current['parity'] == 2, current
                assert current['reconfigurations'] == before['reconfigurations'], current
                assert current['supported'] == (baud != 38400 and baud != 1200), current
                result['checks'].append(dict(baud=baud, parity=parity, status=current))
                print(f'PASS {args.mode}: PC {baud}/8{parity}1, UART remains 9600/8E1', flush=True)
        result['passed'] = True
    except Exception as error:
        result.update(passed=False, error=str(error))
        raise
    finally:
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(result, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
