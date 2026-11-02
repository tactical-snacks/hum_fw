# HUM – Engine Frequency Brackets

Source: phone audio

## States

| State | Clip time | Fundamental (1x) | RPM* |
|---|---|---|---|
| Off | 3–9 s, 83 s+ | none | 0 |
| Transition | 53–59 s | 72–103 Hz | — |
| High RPM | 72–81 s | ~107.5 Hz | ~6,450 |

*Assumes 1x = crank speed.

## Brackets (1x)

1. **Off:** below idle bracket
2. **Idle:** 30–55 Hz
3. **Operating:** 65–150 Hz

## Firmware notes

- Compare band energy (40–55 Hz vs 95–120 Hz) with Goertzel; no FFT needed. <<< learn this
- Accelerometer: 800 Hz ODR, bandwidth ODR/4. 400 Hz ODR aliases the 322 Hz harmonic to ~78 Hz. <<< also learn this
- Capture 512 samples (0.64 s), duty-cycled, with a wake-up interrupt for engine start.

## To verify

- Thresholds come from audio; confirm with raw accelerometer captures from the mounted board.