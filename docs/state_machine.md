# HUM – State Machine

What the logger does with each confirmed bracket. How the brackets are detected is in [Engine frequency brackets](hum_engine_freq.md).

## States

1. **Off**
2. **Idle**
3. **Operating**

## Transitions

| Transition | Actions |
|---|---|
| Off → Idle | 1) enable short sleep of 1 minute between wake and sample; 2) start tracking idle duration |
| Off → Operating | 1) enable short sleep of 1 minute between wake and sample; 2) start tracking operating duration |
| Idle → Operating | 1) store idle duration in EEPROM; 2) start tracking operating duration |
| Operating → Idle | 1) store operating duration in EEPROM; 2) start tracking idle duration |
| Idle → Off | 1) store idle duration in EEPROM; 2) deep sleep, woken by the accel interrupt |
| Operating → Off | 1) store operating duration in EEPROM; 2) deep sleep, woken by the accel interrupt |

## Button

A press wakes the board if it's asleep and prints the durations over serial with a timestamp.
