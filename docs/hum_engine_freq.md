# HUM – Engine Frequency Brackets

Source: initially phone audio ... but this turned out to be problematic because this engine skip fires at idle RPM at a rate around 10 Hz. However, the crank is actually spinning near 40 Hz.

## States

| State | Bracket (1x) | RPM* | Measured (accel, 2026-11-01 to 11-04) |
|---|---|---|---|
| 1 Off | below the amplitude gate | 0 | ~2 mg rms at rest; hand handling 50–500 mg |
| 2 Idle | 30–55 Hz | 1,800–3,300 (spec 2,700) | 37.5–43.75 Hz, 450–2,100 mg rms |
| 3 Operating | 65–150 Hz | 3,900–9,000 (max 6,550 = 109 Hz) | 87.5–100 Hz, 500–1,600 mg rms |

*Assumes 1x = crank speed; RPM = Hz × 60.

## Brackets (1x)

1. **Off:** below idle bracket
2. **Idle:** 30–55 Hz
3. **Operating:** 65–150 Hz

## Amplitude Gating

Sloped line from 30hz,700mg to 100hz,200mg.

## Confirmation Counter

Take 2-3 consecutive readings to verify state.

## Detection

- Compare band energy (idle 31.25–56.25 Hz vs operating 62.5–150 Hz) with Goertzel; no FFT needed. <<< learn this
- Accelerometer: 800 Hz ODR, bandwidth ODR/4, ±8 g. 400 Hz ODR aliases the 322 Hz harmonic to ~78 Hz. <<< also learn this
- Capture 128-sample blocks (0.16 s, 6.25 Hz bins); one reading is 4 blocks (512 samples, 0.64 s), duty-cycled, with a wake-up interrupt for engine start.

What the logger does with each state is in [State machine](state_machine.md).