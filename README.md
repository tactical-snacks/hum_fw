# hum

Hour Utilization Monitor: a coin-cell data logger that timestamps machine run time using an accelerometer and RTC.

## Hardware

| Function | Part |
|---|---|
| MCU | STM32U031K8U6 (Cortex-M0+, UFQFPN-32) |
| RTC clock | 32.768 kHz crystal, ABS07-120 |
| Accelerometer | LIS2DTW12 (I²C, INT1 → PA0, INT2 → PA2) |
| Storage | 24FC512, 64 KB I²C EEPROM |
| Power | CR2032 in Keystone 3034 holder |
| UI | Button (PA1), green LED (PA8) |
| Debug | SWD, ARM 10-pin header |
| Comms | UART, PA9 TX / PA10 RX |

## Docs

- [Schematic](docs/hum_hw.pdf) (KiCad export from the `hum_hw` repo)
- Datasheets:
  - [STM32U031](docs/STMicro%20STM32U031%20Datasheet.pdf)
  - [LIS2DTW12](docs/STMicro%20LIS2DTW12TR%20Datasheet.pdf)
  - [24FC512](docs/Microchip%2024FC512%20Datasheet.pdf)
- [Engine frequency brackets](docs/hum_engine_freq.md): idle, operating, and off bands for run detection
- [State machine](docs/state_machine.md): sleep, duration tracking and EEPROM writes for each state change
- [Engine recordings](docs/recording.zip): WAV clips of cranking, cold idle, transition, and high RPM
- [Bench tools](tools/tools.md): setting the RTC, serial capture, crystal startup test

## Dev
- [x] hello world
- [x] interface with accel, eep
- [x] rtc setup
- [x] freq bracket indicate - LED/serial output current freq bracket
- [ ] bracket time - serial output time in bracket for a continuous session
- [ ] store and retrieve idle/operating durations
- [ ] timestamp

# Changelog

## [UNRELEASED]

### Size

| | Used | Total | |
|---|---:|---:|---:|
| Flash | 46,780 B | 64 KB | 71% |
| RAM | 2,928 B | 12 KB | 24% |

### Added

- LIS2DTW12 accelerometer bring-up: WHO_AM_I check, X/Y/Z in mg over UART at 1 Hz
- 24FC512 EEPROM bring-up: boot counter
- RTC on LSE: set over UART with [tools/set_rtc.py](tools/tools.md#set_rtcpy), kept through resets and reflashing, timestamp on button print, LED flashes rapidly (100 ms on/off) while unset
- Sleep: MCU in Stop 2, EEPROM in standby; a button wake flashes the LED, prints status and stays up 10 s blipping the state; no sleep while the RTC is unset
- Temperature from the accel's sensor in the status line
- Frequency brackets ([docs](docs/hum_engine_freq.md)): accel at 800 Hz, +/-8 g, Goertzel over 31.25-150 Hz on all axes every 0.64 s; the stronger band picks idle (31.25-56.25 Hz) or operating (62.5-150 Hz) if its rms clears a sloped gate (700 mg at 30 Hz to 200 mg at 100 Hz), else off; the bracket changes after 2 agreeing frames; `HUM_EN` (off) also runs it all through the awake window with a `hum:` line and 1/2/3 LED blips per frame
- EEPROM erase: `ERASE` over UART, accepted by a button press within 10 s ([tools.md](tools/tools.md#erasing-the-eeprom))
- EEPROM map in `main.cpp`: unit number 0x0000, boot count 0x0004, idle and operating duration totals (seconds) at 0x0008 and 0x000C, durations not written yet
- State machine ([docs](docs/state_machine.md)): off, idle, operating; every wake samples until 2 readings agree (max 5); idle/operating light-sleep 60 s (5 s with `DBG_EN`) on the RTC wake-up timer with the accel off, off deep-sleeps until the button or motion (accel low-power wake-up on INT1, `ACC_WAKE_EN`); leaving idle or operating adds its duration to the EEPROM total; transitions print a line; status line totals include the session in progress; `DBG_EN` (on for field testing) adds timestamped `DBG` lines for each sample, sleep and wake, and switches to a test timescale: 5 s light sleep, status totals in seconds
- Unit number: set with `U<number>` over UART ([tools.md](tools/tools.md#setting-the-unit-number)), printed at boot and in the status line
- Status line on wake and button press: time, unit, temperature, supply, idle and operating totals in minutes (with `DBG_EN`: tagged `DBG`, totals in seconds, e.g. `DBG u2 ... id0s op0s`), e.g. `20261105-21:50:40 u2 23.7C 2704mV id0m op0m`
- Boot line `hum_fw <git describe>` from [tools/git_rev.py](tools/tools.md#git_revpy), after a blank line

