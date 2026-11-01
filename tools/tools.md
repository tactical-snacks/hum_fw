# Bench tools

Scripts for bring-up and bench testing. Run them from the repo root.

The Python scripts use pyserial from PlatformIO's virtual environment, so nothing extra needs installing:

| OS | Python |
|---|---|
| Windows | `~/.platformio/penv/Scripts/python.exe` |
| Linux / macOS | `~/.platformio/penv/bin/python` |

The examples below use the Windows path and `COM12`; substitute your own.

## Serial connection

The firmware talks on USART1 at 115200 baud, 8N1, through the 3-pin header J2:

| J2 pin | Signal | Connect to adapter |
|---|---|---|
| 1 | PA9, board TX | RX |
| 2 | PA10, board RX (1 kΩ series) | TX |
| 3 | GND | GND |

Only one program can open a serial port at a time. Close any serial monitor before running a script, or the script fails with `Access is denied`.

## set_rtc.py

Sets the board's real-time clock from the PC's clock.

```
~/.platformio/penv/Scripts/python.exe -I tools/set_rtc.py COM12
```

It sends `T<YYYYMMDD-HH:MM:SS>` timed to arrive on the next whole second, then prints the board's reply, e.g. `rtc: set 20261031-23:20:24`. Local time is sent by default; add `--utc` for UTC.

The clock stays set through resets and reflashing. It is lost when the coin cell is removed. While the clock is unset, the LED blips for 5 ms once a second and the boot message reads `rtc: unset`.

You can also set the clock by hand from any serial terminal by typing the same command, e.g. `T20261031-14:05:00` followed by Enter.

## serial_capture.py

Records a serial port for a fixed time and prints what it receives.

```
~/.platformio/penv/Scripts/python.exe -I tools/serial_capture.py COM12 15
```

Arguments are port, seconds, and an optional baud rate (default 115200).

## lse_drive_timing.tcl

Times how long the 32.768 kHz crystal takes to start at each of the four drive strengths, then stops the crystal and reboots the board so the firmware sees a cold start. Use it to check the crystal circuit, for example after rework or cleaning.

```
~/.platformio/packages/tool-openocd/bin/openocd.exe -s ~/.platformio/packages/tool-openocd/openocd/scripts -f interface/stlink.cfg -c "transport select swd" -f target/stm32u0x.cfg -f tools/lse_drive_timing.tcl
```

A healthy board starts at every level in under about 300 ms. It needs an ST-Link connected to J3.

Running it also clears the RTC, because the clock lives in the same backup domain as the crystal settings. Run `set_rtc.py` afterwards.

## Planned

- Make `set_rtc.py` friendlier: find the board's port automatically, show the clock before and after, and give clear messages when the port is busy or the board doesn't answer.
