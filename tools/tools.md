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

Once the clock is set, the board sleeps and can't receive serial commands. Press its button first; it stays awake for 10 s after a wake or button press, and each command received restarts that window.

The clock stays set through resets and reflashing. It is lost when the coin cell is removed. While the clock is unset, the LED flashes rapidly (100 ms on, 100 ms off) and the boot message reads `rtc: unset`. Once it's set, the LED blips the state while awake: 1 blip for off, 2 for idle, 3 for operating.

You can also set the clock by hand from any serial terminal by typing the same command, e.g. `T20261031-14:05:00` followed by Enter.

## Setting the unit number

Type `U` and the number followed by Enter in a serial terminal while the board is awake, e.g. `U1001`. The board stores it in the EEPROM, reads it back and replies `unit: set 1001`. Numbers run from 0 to 4294967294. The unit number prints at boot (`unit: 1001`, or `unit: unset, send U<number>` when blank) and in every status line after the timestamp.

## RTC calibration

The 32.768 kHz crystal sets how fast the clock runs. To correct a board that gains or loses time, measure its drift and send it with `C<ppm>`: positive if the clock runs fast, at most one decimal place, within ±480.0. For example, `C51.8` for a clock that gains 4.5 s a day. The board applies the correction through the RTC's smooth calibration (steps of 0.954 ppm), stores it in the EEPROM, and replies `rtc: cal +51.8 ppm`. It prints the value at boot (`rtc: cal +51.8 ppm`) and reapplies it. `C0` removes the correction.

Each crystal is different, so measure every unit:

1. Send `C0` if the unit has a correction, then set the clock with `set_rtc.py`.
2. Straight away, run `serial_capture.py 120 --stamp` and press the button 5 or 6 times at uneven intervals.
3. Leave the unit for 48 hours or more, then repeat step 2.
4. Each status line bounds the offset: the board is between (board time − PC time) and that plus 1 s ahead. Take the largest lower bound and the smallest upper bound from each capture. The drift in ppm is (end offset − start offset) / elapsed seconds × 1,000,000.

Measuring with a correction in place gives what's left over: send the old value plus that residual. `ERASE` clears the stored value; the correction then lasts until the next boot.

## Erasing the EEPROM

Type `ERASE` followed by Enter in a serial terminal while the board is awake. The board replies `eep: press the button within 10 s to erase all 64 KB`; press the button to accept. It writes every byte back to 0xFF and reads each page back, which takes about 5.4 s, then prints `eep: erased in … ms`. With no press in 10 s it prints `eep: erase cancelled, no button press` and nothing changes.

This clears the unit number and resets the boot count, so the next boot reads `unit: unset` and `eep: boot count 1`. The RTC is not affected.

## serial_capture.py

Records a serial port for a fixed time and prints what it receives.

```
~/.platformio/penv/Scripts/python.exe -I tools/serial_capture.py 15
```

The argument is the capture time in seconds. `--stamp` prefixes each line with the PC time it arrived, to the millisecond (`[PC 20261110-10:30:51.123] ...`), for checking RTC drift against the PC clock.

## send.py

Sends one command line to the board and prints the reply, so commands like `C51.8` or `U1001` don't need a serial terminal. Press the board's button first so it's awake.

```
~/.platformio/penv/Scripts/python.exe -I tools/send.py C51.8
```

`--wait` sets how long to print replies for (default 1 s).

## lse_drive_timing.tcl

Times how long the 32.768 kHz crystal takes to start at each of the four drive strengths, then stops the crystal and reboots the board so the firmware sees a cold start. Use it to check the crystal circuit, for example after rework or cleaning.

```
~/.platformio/packages/tool-openocd/bin/openocd.exe -c "reset_config srst_only srst_nogate connect_assert_srst" -s ~/.platformio/packages/tool-openocd/openocd/scripts -f interface/stlink.cfg -c "transport select swd" -f target/stm32u0x.cfg -f tools/lse_drive_timing.tcl
```

A healthy board starts at every level in under about 300 ms. It needs an ST-Link connected to J3; the `reset_config` option connects under reset, which is needed because the firmware sleeps with SWD off.

Running it also clears the RTC, because the clock lives in the same backup domain as the crystal settings. Run `set_rtc.py` afterwards.

## git_rev.py

Not run by hand: PlatformIO runs it before each build (`extra_scripts` in `platformio.ini`). It passes `git describe --always --dirty --tags` to the firmware, which prints it at boot, e.g. `hum_fw 66349fb` or `hum_fw 66349fb-dirty` when built with uncommitted changes.

## Planned

- Make `set_rtc.py` friendlier: show the clock before and after, prompt to wake the board, and give clear messages when the port is busy or the board doesn't answer.
