# HUM – State Machine

What the logger does with each confirmed bracket. How the brackets are detected is in [Engine frequency brackets](hum_engine_freq.md).

## States

1. **Off**
2. **Idle**
3. **Operating**

## Sleep

| Mode | Wakes on | Used in |
|---|---|---|
| **Light sleep** | timer: after X minutes (RTC wake-up), or a button press | Idle, Operating: wake, sample, sleep again |
| **Deep sleep** | external interrupt only: button press or accel interrupt; never by time | Off |

Both are Stop 2 on the MCU; the difference is whether the RTC wake-up timer is armed.

## Transitions

| Transition | Actions |
|---|---|
| Off → Idle | 1) switch to light sleep, waking every 1 minute to sample; 2) start tracking idle duration |
| Off → Operating | 1) switch to light sleep, waking every 1 minute to sample; 2) start tracking operating duration |
| Idle → Operating | 1) store idle duration in EEPROM; 2) start tracking operating duration |
| Operating → Idle | 1) store operating duration in EEPROM; 2) start tracking idle duration |
| Idle → Off | 1) store idle duration in EEPROM; 2) deep sleep |
| Operating → Off | 1) store operating duration in EEPROM; 2) deep sleep |

## Sampling

Every wake ends in one sample: a timer or motion wake samples at once; a button wake first stays up 10 s for serial commands. A sample takes 0.64 s readings until 2 in a row agree, at most 5 (3.2 s). If none agree, the state stays as it was.

## Durations

- Measured with the RTC, so the clock must be set; while it's unset the board stays awake and doesn't sample.
- Stored in EEPROM as running totals in seconds when a state is left (addresses in `main.cpp`). Transitions are only seen at samples, so durations are accurate to about one light-sleep interval.
- The status line shows each total plus the session in progress, in minutes.
- Setting the clock mid-session restarts that session's timing, so the correction isn't counted.
- The state and the start of the current session are kept only in RAM: a reset or battery swap loses the session in progress (stored totals are kept) and starts again from Off.
- Each transition prints a line, e.g. `20261105-22:10:05 off->operating`.

## Button

A press wakes the board from light or deep sleep and prints the durations over serial with a timestamp. While awake, the LED blips the current state once a second.

## Accelerometer

Its interrupt wakes the MCU from deep sleep only; in light sleep it's disarmed and powered down, and the timer does the waking.