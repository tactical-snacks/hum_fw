# HUM – Power budget

Status: estimate, 2026-11-12, firmware v0.1.0 plus unreleased changes. Nothing below has been measured yet; see [Verify](#verify).

hum_hw's `power_budget.md` (2–3 µA, 8–12 years) uses typical part figures and counts a wake as ~1 ms at 1 mA. In this firmware a wake is a 1.3–3.3 s measurement at ~2 mA. That measurement cost is what sets battery life while the machine runs.

## Method

- **Budget** column: datasheet **max** at **55 °C**, **3.3 V** (fresh cell; the board sits on or near a machine, possibly in sun). Where a datasheet gives only a typical figure, typ + 25–50 %. Unknowns get a stated allowance.
- **Typ** column: datasheet typical at 25 °C, 3.0 V, short measurements. For comparison only.
- Usable cell: **150 mAh** budget, 220 mAh typ ([Battery](#battery)).
- Production build: `DBG_EN` off, so 60 s light sleep. Main currently has `DBG_EN` **on** (5 s); see [Traps](#traps).
- Flash stays powered in Stop 2 (the [RTC wake HardFault](rtc_wake_hardfault.md) workaround).
- Charge is in mAs (= mC). 1 mAs per day averages 0.0116 µA.

## Sleep floor

Stop 2 in every state. Light sleep (idle, operating) powers the accel down; deep sleep (off) leaves it in motion wake-up.

| Item | Source | Typ (µA) | Budget (µA) |
|---|---|---:|---:|
| MCU Stop 2, RTC on LSE crystal | DS14581 Table 39: LSE low-drive, EN_ULP = 0, LPCAL = 0 | 0.85 | 7.0 |
| LSE at medium-low drive (Table 39 is low-drive) | DS14581 IDD(LSE): 315 − 250 nA | 0.07 | 0.1 |
| Flash kept powered in Stop 2 | not in the datasheet: allowance | 0.5 | 2.0 |
| 24FC512 standby | 1 µA max (at 5.5 V); no typ given | 0.1 | 1.0 |
| Board leakage: MLCCs (~67 µF, C3 is 47 µF), I/O pins | allowance | 0.05 | 0.5 |
| Light: accel powered down | DS12825: 50 nA typ | 0.05 | 0.1 |
| Deep: accel LP1, 12.5 Hz, wake-up on INT1 | DS12825 Table 10: 1 µA typ at 1.8 V, no max | 1.0 | 1.5 |
| **Light sleep floor** | | **1.6** | **10.7** |
| **Deep sleep floor** | | **2.6** | **12.1** |

The MCU dominates the budget, and Stop 2 leakage climbs steeply with temperature. Deep sleep floor at datasheet max:

| Temperature | 30 °C | 55 °C | 85 °C |
|---|---:|---:|---:|
| Deep sleep floor | 8.0 µA | 12.1 µA | 30.1 µA |
| Shelf life on 150 mAh | 2.2 y | 1.4 y | 0.6 y |

## Awake current

While awake, `loop()` busy-spins with no `wfi`, so the MCU draws full run current the whole time. On every pass `hum_service()` also polls the accel FIFO count over I²C, so the bus is close to 100 % busy.

| Item | Source | Typ (mA) | Budget (mA) |
|---|---|---:|---:|
| MCU run, MSI 16 MHz, range 2, from flash | DS14581 Tables 26 / 28 (ART off is worse) | 1.10 | 1.35 |
| Peripheral clocks (I²C1, USART1, RTC, GPIO) | DS14581 Table 43, range 2: all APB 13.6 µA/MHz × 16 | 0.04 | 0.22 |
| Accel 800 Hz, high-performance, low-noise on | DS12825 Table 11: 120 µA typ at 1.8 V, no max | 0.12 | 0.15 |
| I²C pull-ups R3, R4 (4.7 k) | each line low about half the time: 2 × 3.3 V / 4.7 k × 0.5 | 0.40 | 0.70 |
| LED state blips | at most 3 × 5 ms per second; ≤ 0.2 mA lit through 4.7 k (green, Vf ≥ 2.4 V) | 0 | 0.003 |
| **Total** | | **1.7** | **2.42, use 2.5** |

## Charge per event

| Event | Awake time, typ / budget | Charge, typ / budget |
|---|---|---|
| Timer wake (idle, operating) | 1.4 s (2 frames agree) / 3.3 s (all 5 frames + 0.1 s overhead) | 2.4 / 8.3 mAs |
| Motion wake (off) | as above, + 250 ms accel settle + up to 200 ms waiting for INT1 to drop | 2.8 / 9.4 mAs |
| Button wake | as a motion wake, + the 10 s awake window | 19.8 / 34.4 mAs |
| EEPROM write per transition | 5 ms × 5 mA max (at 5.5 V) | ~0 / 0.025 mAs |

EEPROM writes are negligible: even 100 transitions a day average 0.03 µA.

## State averages

| State | Typ | Budget |
|---|---:|---:|
| Off (deep sleep), undisturbed | 2.6 µA | 12.1 µA |
| Idle / operating, 60 s light sleep | 40 µA | 141 µA |
| Idle / operating, `DBG_EN` build (5 s) | 373 µA | 1.0 mA |
| Each motion wake per day | +0.03 µA | +0.11 µA |
| Each button press per day | +0.23 µA | +0.40 µA |

Running average = (wake charge + light floor × 60 s) / (60 s + awake time). At budget, one running hour costs 0.14 mAh, as much as 11.6 hours off. **A cell lasts about 1,070 running hours at budget**, ~5,450 typ, before any off-time drain.

## Battery

- CR2032: 220 mAh nominal (to 2.0 V at ~0.2 mA continuous).
- Budget uses **150 mAh** (×0.68), covering:
  - 2.5 mA pulses lasting seconds, above the rated drain
  - cold
  - self-discharge (~1 %/year, more when hot)
  - cell-to-cell spread
- End of life is 2.0 V under load. Every part runs to ~1.7 V (MCU BOR0 1.62–1.70 V, accel 1.62 V, EEPROM 1.7 V). The margin covers the drop across an aged cell's internal resistance: ~100 Ω or more × 2.5 mA = 0.25 V.
- C3 (47 µF) only rides through millisecond pulses (τ ≈ 5 ms at 100 Ω), not a 3 s measurement.

## Life by usage

Assumes 24 motion wakes a day while off (handling, wind, nearby machinery) and 2 button presses a day.

| Running h/day | Avg, typ | Life, typ (220 mAh) | Avg, budget | Life, budget (150 mAh) |
|---:|---:|---:|---:|---:|
| 0 (installed, unused) | 3.8 µA | 6.6 y | 15.5 µA | 13 months |
| 1 | 5.4 µA | 4.7 y | 20.8 µA | 10 months |
| 4 | 10.1 µA | 2.5 y | 36.9 µA | 5.5 months |
| 8 | 16.4 µA | 1.5 y | 58.3 µA | 3.5 months |
| 24 | 40.8 µA | 7.4 months | 141 µA | 6 weeks |

On a shelf (off, undisturbed), typ is limited by the cell's shelf life (~10 y). Budget is 1.4 y.

## Traps

Each of these drains a cell in days:

| Trap | Why | Typ | Budget |
|---|---|---:|---:|
| Clock unset (every battery swap) | Power loss resets the RTC. Until it's set over UART the board stays awake, with the accel at 800 Hz and the LED flashing (2.6 mA) | ~5 days | ~2.5 days |
| `DBG_EN` on (as on main now) | 5 s light sleep: the board measures for up to 3.3 of every 8.3 s | 590 running h | 150 running h |
| Continuous vibration while off | Transport, or vibration over the 62.5 mg wake threshold but under the amplitude gate. INT1 is still high at each sleep, so the board re-wakes at once and stays awake at 2.5 mA | ~5 days | ~60 h of vibration |
| `DBG_STOP` left set after flashing | Clocks keep running in Stop. Not quantified; pull the battery after flashing ([docs](rtc_wake_hardfault.md)) | ? | ? |

## What moves the numbers

- **Awake time per timer wake** sets the running cost. Cutting the budget wake from 3.3 s to 1.3 s brings running from 141 to 64 µA.
- **Light-sleep interval**: 120 s instead of 60 s roughly halves the running cost, and durations become accurate to 2 minutes instead of 1.
- **Sleeping between FIFO drains** (FIFO threshold interrupt plus `wfi`) instead of busy-polling would remove most of the MCU run current and pull-up current during a measurement. At 800 Hz the FIFO holds 40 ms.
- **The planned `DBG_STOP` fix** lets the flash power down in Stop 2, which drops the 2 µA allowance.
- **Temperature**: the budget assumes 55 °C. If the unit sits at room temperature, the 30 °C column applies to the sleep floor.

## Verify

- Measure across R2 (0 Ω, current sense): lift it and put the meter across its pads.
- Pull the battery after flashing and before measuring, so `DBG_STOP` is clear.
- The meter must range from µA to mA without glitching: a wake steps from ~10 µA to ~2.5 mA.
- Measure, in order:
  1. deep sleep floor
  2. light sleep floor
  3. charge of one timer wake (shunt and scope, or a charge-integrating meter)
  4. awake current
- Replace the flash and board-leakage allowances with the measured sleep floors. Then repeat the floor measurements with the `DBG_STOP` fix in place.
