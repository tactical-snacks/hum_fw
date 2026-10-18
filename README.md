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

# Changelog

## [UNRELEASED]

### Added