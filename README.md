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
- [RTC wake HardFault](docs/rtc_wake_hardfault.md): `DBG_STOP` left set after flashing faults the first RTC timer wake; workaround and planned fix
- [Engine recordings](docs/recording.zip): WAV clips of cranking, cold idle, transition, and high RPM
- [Bench tools](tools/tools.md): setting the RTC, serial capture, crystal startup test

## Dev
- [x] hello world
- [x] interface with accel, eep
- [x] rtc setup
- [x] freq bracket indicate - LED/serial output current freq bracket
- [x] bracket time - serial output time in bracket
- [ ] store and retrieve idle/operating durations
- [ ] sessions with timestamps

## AI disclosure

This project was written with the help of AI (Claude Code). The frequency brackets and amplitude gate come from accelerometer measurements on a running engine ([docs](docs/hum_engine_freq.md)). The firmware was tested on [hum_hw](https://github.com/tactical-snacks/hum_hw).

# Changelog

## [Unreleased]

### Size

| | Used | Total | |
|---|---:|---:|---:|
| Flash | 47,764 B | 64 KB | 73% |
| RAM | 2,932 B | 12 KB | 24% |

### Added

- RTC calibration: `C<ppm>` stores the measured drift in the EEPROM and corrects it with the RTC's smooth calibration, reapplied at boot ([tools.md](tools/tools.md#rtc-calibration)); unit 1 measured +51.8 ppm
- [send.py](tools/tools.md#sendpy): send one command and print the reply

## [0.1.0] - 2026-11-10

### Size

| | Used | Total | |
|---|---:|---:|---:|
| Flash | 46,772 B | 64 KB | 71% |
| RAM | 2,928 B | 12 KB | 24% |

### Added

- State machine ([docs](docs/state_machine.md)): off, idle, operating; idle and operating light-sleep on the RTC timer, off deep-sleeps until the button or motion
- Frequency brackets ([docs](docs/hum_engine_freq.md)): Goertzel on the accel at 800 Hz picks idle or operating behind a sloped amplitude gate
- EEPROM totals of idle and operating time, unit number and boot count
- RTC on LSE, set with [set_rtc.py](tools/tools.md#set_rtcpy); LED fast-flashes while unset
- Sleep: MCU in Stop 2, EEPROM in standby; a button wake prints status and stays up 10 s
- Status line, e.g. `20261105-21:50:40 u2 23.7C 2704mV id0m op0m`
- UART commands: `U<number>` sets the unit number, `ERASE` clears the EEPROM after a button press ([tools.md](tools/tools.md))
- Boot line with the git revision
- `DBG_EN` (on): `DBG` lines per sample, sleep and wake; 5 s light sleep, totals in seconds

### Known issues

- Flash stays powered in Stop 2 to avoid an RTC wake HardFault, costing sleep current ([docs](docs/rtc_wake_hardfault.md))
