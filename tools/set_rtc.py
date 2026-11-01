"""Set the board's RTC from this PC's clock over the debug UART.

usage: python -I tools/set_rtc.py PORT [--utc] [--baud BAUD]
Sends T<YYYYMMDD-HH:MM:SS> at the start of the next second and prints the board's reply.
Uses pyserial from the PlatformIO venv (~/.platformio/penv).
"""
import argparse
import datetime
import time

import serial

# time to clock out the 19-byte command at 115200 baud, so the board receives it on the second
SEND_LEAD_S = 19 * 10 / 115200


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('--utc', action='store_true', help='send UTC instead of local time')
    ap.add_argument('--baud', type=int, default=115200)
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as s:
        s.reset_input_buffer()

        # wait for the next whole second, minus the time on the wire
        now = datetime.datetime.now(datetime.timezone.utc if args.utc else None)
        target = now.replace(microsecond=0) + datetime.timedelta(seconds=1)
        time.sleep(max(0.0, (target - now).total_seconds() - SEND_LEAD_S))

        s.write(('T' + target.strftime('%Y%m%d-%H:%M:%S') + '\n').encode('ascii'))

        end = time.time() + 1.0
        reply = b''
        while time.time() < end and not reply.endswith(b'\n'):
            reply += s.read(64)
        print(reply.decode('ascii', 'replace').strip() or 'no reply (is board RX wired?)')


if __name__ == '__main__':
    main()
