"""Capture a serial port for a fixed time and echo it to stdout.

usage: python -I tools/serial_capture.py PORT SECONDS [BAUD]
Uses pyserial from the PlatformIO venv (~/.platformio/penv).
"""
import sys
import time

import serial


def main():
    port = sys.argv[1]
    secs = float(sys.argv[2])
    baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200

    with serial.Serial(port, baud, timeout=0.2) as s:
        end = time.time() + secs
        while time.time() < end:
            data = s.read(256)
            if data:
                sys.stdout.write(data.decode('ascii', 'replace').replace('\r', ''))
                sys.stdout.flush()


if __name__ == '__main__':
    main()
