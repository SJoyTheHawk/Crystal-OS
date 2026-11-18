#!/usr/bin/env python3
"""Capture UART logs; optionally send manual netdiag commands. Never flashes."""
import argparse
import datetime
from pathlib import Path
import select
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=1000000)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--minutes', type=float, default=0, help='0 records until quit/Ctrl-C')
    args = parser.parse_args()
    if args.minutes < 0:
        parser.error('--minutes cannot be negative')
    import serial  # Available in the ESP-IDF Python environment.
    args.log.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.2, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = args.port
    tz = datetime.timezone(datetime.timedelta(hours=8))
    with args.log.open('x', buffering=1) as output:
        def record(line):
            output.write(datetime.datetime.now(tz).isoformat(timespec='milliseconds') + ' ' + line + '\n')

        port.open()
        pending = b''
        started = time.monotonic()
        last_status = started
        stdin_open = True
        total = 0
        print('Recording', args.log, flush=True)
        print('Type # note, netdiag <command>, or quit. No automatic probes.', flush=True)
        record('# capture opened; no explicit reset requested; check logs for boot on serial open')
        try:
            while not args.minutes or time.monotonic() - started < args.minutes * 60:
                data = port.read(port.in_waiting or 1)
                total += len(data)
                pending += data
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    text = line.decode('utf-8', errors='replace').rstrip('\r')
                    record(text)
                    if any(key in text.lower() for key in ('wifi_check:', 'disconnected', 'ip acquired',
                            'active route', 'transport diagnostic', 'timed out', 'panic', 'hosted fw versions')):
                        print(text, flush=True)
                if stdin_open and select.select([sys.stdin], [], [], 0)[0]:
                    command = sys.stdin.readline()
                    if not command:
                        stdin_open = False
                    else:
                        command = command.strip()
                        if command == 'quit':
                            break
                        if command.startswith('#'):
                            record(command)
                        elif command.startswith('netdiag '):
                            if len(command.encode()) > 380:
                                print('Command too long', flush=True)
                            else:
                                record('# TX ' + command)
                                port.write((command + '\n').encode())
                                port.flush()
                        elif command:
                            print('Accepted: # note | netdiag <command> | quit', flush=True)
                if time.monotonic() - last_status >= 60:
                    print('Captured %.1f minutes, %d bytes' % ((time.monotonic() - started) / 60, total), flush=True)
                    last_status = time.monotonic()
        except KeyboardInterrupt:
            pass
        finally:
            if pending:
                record(pending.decode('utf-8', errors='replace'))
            record('# capture closed')
            port.close()
            print('Saved', args.log, flush=True)


if __name__ == '__main__':
    main()
