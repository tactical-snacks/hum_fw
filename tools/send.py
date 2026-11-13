"""Send one line command to the board and print its reply.

usage: python -I tools/send.py COMMAND [--port PORT] [--baud BAUD] [--wait SECONDS]
e.g. send.py C51.8, send.py U1001. PORT and BAUD default to monitor_port / monitor_speed from
platformio.ini (+ platformio_local.ini). The board must be awake: press its button first.
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
    ap.add_argument('command')
    ap.add_argument('--port', default=port)
    ap.add_argument('--baud', type=int, default=baud)
    ap.add_argument('--wait', type=float, default=1.0, help='how long to print replies for')
    args = ap.parse_args()
    if not args.port:
        ap.error('no --port given and no monitor_port in platformio_local.ini')

    with serial.Serial(args.port, args.baud, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write((args.command + '\n').encode('ascii'))
        end = time.time() + args.wait
        reply = b''
        while time.time() < end:
            reply += s.read(256)
    text = reply.decode('ascii', 'replace').replace('\r', '').strip()
    print(text or 'no reply: is the board awake (press its button) and is RX wired?')
    return 0 if text else 1


if __name__ == '__main__':
    sys.exit(main())
