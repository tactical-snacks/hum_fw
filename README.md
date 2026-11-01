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
- [Engine recordings](docs/recording.zip): WAV clips of cranking, cold idle, transition, and high RPM
- [Bench tools](tools/tools.md): setting the RTC, serial capture, crystal startup test

## Dev
- [x] hello world
- [x] interface with accel, eep
- [x] rtc setup
- [ ] freq bracket indicate - LED/serial output current freq bracket
- [ ] bracket time - serial output time in bracket for a continuous session
- [ ] store and retrieve - keep running tally
- [ ] timestamp

# Changelog

## [UNRELEASED]

### Size

| | Used | Total | |
|---|---:|---:|---:|
| Flash | 42,100 B | 64 KB | 64% |
| RAM | 2,192 B | 12 KB | 18% |

### Added

- LIS2DTW12 accelerometer bring-up: WHO_AM_I check, X/Y/Z in mg over UART at 1 Hz
- 24FC512 EEPROM bring-up: boot counter at 0x0000
- RTC on LSE: set over UART with [tools/set_rtc.py](tools/tools.md#set_rtcpy), kept through resets and reflashing, timestamp on button print, LED blips 5 ms/s while unset
- Sleep: MCU in Stop 2, accel in low-power mode 1 with a motion wake-up on INT1, EEPROM in standby; wakes on motion or button, flashes the LED and prints status, stays up 5 s; no sleep while the RTC is unset
- Temperature from the accel's sensor in the status line

