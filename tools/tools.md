# Bench tools

Scripts for bring-up and bench testing. Run them from the repo root.

The Python scripts use pyserial from PlatformIO's virtual environment, so nothing extra needs installing:

| OS | Python |
|---|---|
| Windows | `~/.platformio/penv/Scripts/python.exe` |
| Linux / macOS | `~/.platformio/penv/bin/python` |

The examples below use the Windows path.

## Serial port and baud rate

The scripts take the port and baud rate from PlatformIO's config, the same values `pio device monitor` uses:

- `monitor_speed` is in `platformio.ini`.
- `monitor_port` is machine-specific, so it goes in `platformio_local.ini` at the repo root. That file is gitignored; create it on each machine:

  ```ini
  [env:genericSTM32U031K8]
  monitor_port = COM12
  ```

Either can be overridden on the command line with `--port` / `--baud`.

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
~/.platformio/penv/Scripts/python.exe -I tools/set_rtc.py
```

It sends `T<YYYYMMDD-HH:MM:SS>` timed to arrive on the next whole second, then prints the board's reply, e.g. `rtc: set 20261031-23:20:24`. Local time is sent by default; add `--utc` for UTC. A port can be given as the first argument instead of using `monitor_port`.

Once the clock is set, the board sleeps and can't receive serial commands. Press its button first; it stays awake for 5 s after a wake or button press, and each command received restarts that window.

The clock stays set through resets and reflashing. It is lost when the coin cell is removed. While the clock is unset, the LED blips for 5 ms once a second and the boot message reads `rtc: unset`.

You can also set the clock by hand from any serial terminal by typing the same command, e.g. `T20261031-14:05:00` followed by Enter.

## serial_capture.py

Records a serial port for a fixed time and prints what it receives.

```
~/.platformio/penv/Scripts/python.exe -I tools/serial_capture.py 15
```

The argument is the capture time in seconds.

## lse_drive_timing.tcl

Times how long the 32.768 kHz crystal takes to start at each of the four drive strengths, then stops the crystal and reboots the board so the firmware sees a cold start. Use it to check the crystal circuit, for example after rework or cleaning.

```
~/.platformio/packages/tool-openocd/bin/openocd.exe -c "reset_config srst_only srst_nogate connect_assert_srst" -s ~/.platformio/packages/tool-openocd/openocd/scripts -f interface/stlink.cfg -c "transport select swd" -f target/stm32u0x.cfg -f tools/lse_drive_timing.tcl
```

A healthy board starts at every level in under about 300 ms. It needs an ST-Link connected to J3; the `reset_config` option connects under reset, which is needed because the firmware sleeps with SWD off.

Running it also clears the RTC, because the clock lives in the same backup domain as the crystal settings. Run `set_rtc.py` afterwards.

## Planned

- Make `set_rtc.py` friendlier: show the clock before and after, prompt to wake the board, and give clear messages when the port is busy or the board doesn't answer.
