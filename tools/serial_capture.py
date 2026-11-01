"""Capture a serial port for a fixed time and echo it to stdout.

usage: python -I tools/serial_capture.py SECONDS [--port PORT] [--baud BAUD]
PORT and BAUD default to monitor_port / monitor_speed from platformio.ini (+ platformio_local.ini).
Uses pyserial and platformio from the PlatformIO venv (~/.platformio/penv).
"""
import argparse
import os
import sys
import time

import serial


def pio_serial_defaults():
    """(monitor_port, monitor_speed) as PlatformIO sees them; port is None if unset."""
    from platformio.project.config import ProjectConfig

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cwd = os.getcwd()
    os.chdir(root)  # extra_configs is resolved against the working directory
    try:
        cfg = ProjectConfig(os.path.join(root, 'platformio.ini'))
        env = 'env:' + cfg.envs()[0]
        return cfg.get(env, 'monitor_port', None), int(cfg.get(env, 'monitor_speed', 115200))
    finally:
        os.chdir(cwd)


def main():
    port, baud = pio_serial_defaults()

    ap = argparse.ArgumentParser()
    ap.add_argument('seconds', type=float)
    ap.add_argument('--port', default=port)
    ap.add_argument('--baud', type=int, default=baud)
    args = ap.parse_args()
    if not args.port:
        ap.error('no --port given and no monitor_port in platformio_local.ini')

    with serial.Serial(args.port, args.baud, timeout=0.2) as s:
        end = time.time() + args.seconds
        while time.time() < end:
            data = s.read(256)
            if data:
                sys.stdout.write(data.decode('ascii', 'replace').replace('\r', ''))
                sys.stdout.flush()


if __name__ == '__main__':
    main()
