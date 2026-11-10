# HUM – RTC wake HardFault (DBG_STOP)

Status: root cause found 2026-11-06. Main runs a workaround (flash kept powered in Stop 2); the proper fix is planned, not yet in.

## Symptom

Field test, unit 2, firmware `4928bce`: the machine idled 2 m 2 s and then operated 6 m 8 s, but nothing was stored. Afterwards the board ignored the button and the serial port. Only pulling and reinserting the battery brought it back.

## What happened

1. Motion woke the board from deep sleep, it sampled idle and went off → idle, then entered light sleep (Stop 2 with the RTC wake-up timer armed).
2. The first timer wake, 60 s later, HardFaulted. The stacked PC was the first flash word after the `wfi` in `HAL_PWREx_EnterSTOP2Mode`. The core sat in the fault handler, so the button had no effect and nothing reached the EEPROM.

Light sleep was new in `d587873`. On the bench a board reads off and only deep-sleeps, so the timer wake had never run before the field test.

## Cause

The fault needs all three of these at once:

- **`DBG_STOP` set in `DBGMCU_CR`.** OpenOCD's `target/stm32u0x.cfg` sets `DBG_STOP | DBG_STANDBY` on every connect, including every `pio run -t upload`. Only a power-on reset clears the register, so a board keeps it set after flashing until its battery is pulled. Unit 2 went to the field like that.
- **A wake by the RTC** (a peripheral wake, not an EXTI pin). Button and motion wakes are EXTI pins and never faulted.
- **Interrupts masked across `wfi`**, as `mcu_sleep()` does to close the race between checking the wake flags and sleeping.

ST has reproduced this on a NUCLEO-U083 (internal ticket 199758, from an ST Community thread reporting it on a U073; a U031 user reported the same in June 2025). It is not in the U031 errata sheet ES0603 as far as we know: the sheet couldn't be fetched to check. The STM32U5 errata (ES0499, ES0553, ES0595) document the same class of fault: debug mode with `DBG_STOP` set, triggered by a peripheral wake other than EXTI.

## Evidence

Bisect harness on branch `rtcdbg` (local): minimal firmware, RTC timer wake every 5 s, HardFault handler that prints the stacked PC over USART1.

| Flash in Stop 2 | Interrupts masked | `DBGMCU_CR` | Result |
|---|---|---|---|
| powered down | no | `DBG_STOP` set | clean, 5/5 wakes |
| powered down | yes | `DBG_STOP` set | HardFault on first wake, 4/4 boots |
| powered down | yes | cleared at boot | clean, 6/6 wakes |
| kept powered | yes | `DBG_STOP` set | clean (workaround now on main) |

ADC, I2C/accel and the EXTI handlers were all off in the harness, so none of them is involved.

## Current state on main

`mcu_sleep()` clears `FPD_STOP` (`951915e`): the flash stays powered in Stop 2. This hides the fault but costs sleep current on every sleep, deep sleep included.

## Planned fix

- At the top of `setup()`: `__HAL_RCC_DBGMCU_CLK_ENABLE(); DBGMCU->CR = 0;`
- Remove the `FPD_STOP` line from `mcu_sleep()`, so the flash powers down in Stop 2 again.
- The cost is no debugging through Stop. Uploads already connect under reset, so flashing is unaffected.

## Until then

- Pull and reinsert the battery after flashing a board that will be measured or taken to the field. That clears `DBG_STOP`.
- Sleep current measured after a flash without a battery pull is inflated: `DBG_STOP` keeps clocks running in Stop.

## References

- [ST Community: hard fault on waking from Stop mode while debugging on U073](https://community.st.com/t5/stm32-mcus-products/hard-fault-on-waking-from-stop-mode-while-debugging-on-u073/m-p/758750/highlight/true)
- [ES0603, STM32U031x4/6/8 device errata](https://www.st.com/resource/en/errata_sheet/es0603-stm32u031x468-device-errata-stmicroelectronics.pdf)
- [ES0499, STM32U575/585 device errata](https://www.st.com/resource/en/errata_sheet/es0499-stm32u575xx-and-stm32u585xx-device-errata-stmicroelectronics.pdf)
- [ES0553, STM32U59x/5Ax device errata](https://www.st.com/resource/en/errata_sheet/es0553-stm32u59xxx-and-stm32u5axxx-device-errata-stmicroelectronics.pdf)
- [ES0595, STM32U5Fx/5Gx device errata](https://www.st.com/resource/en/errata_sheet/es0595-stm32u5fxxx-and-stm32u5gxxx-device-errata-stmicroelectronics.pdf)
