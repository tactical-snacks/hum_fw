#include <Arduino.h>
#include <Wire.h>

#ifndef FW_REV
#define FW_REV "unknown"   // set by tools/git_rev.py
#endif


// pin assignments (Arduino pin names; for HAL use digitalPinToPort() / digitalPinToBitMask())
#define LED_PIN              PA8     // pin 18, active high
#define BTN_PIN              PA1     // pin 7,  active low, pull-up, WKUP3
#define ACC_INT1_PIN         PA0     // pin 6,  WKUP1
#define ACC_INT2_PIN         PA2     // pin 8,  WKUP4
#define I2C_SCL_PIN          PB6     // pin 29, AF4 I2C1
#define I2C_SDA_PIN          PB7     // pin 30, AF4 I2C1
#define UART_TX_PIN          PA9     // pin 19, AF7 USART1
#define UART_RX_PIN          PA10    // pin 20, AF7 USART1

#define EEPROM_I2C_ADDR      0x50U   // 24FC512, A2..A0 = GND
#define ACC_I2C_ADDR         0x19U   // LIS2DTW12, SA0 = VCC

#define UART_BAUD            115200
#define BTN_DEBOUNCE_MS      30U     // no external pull-up or RC on PA1
#define LED_BLIP_ON_MS       5U      // state patterns are 5 ms blips: 1 off, 2 idle, 3 operating
#define LED_BLIP_GAP_MS      150U    // blip spacing within a pattern
#define LED_STATE_PERIOD_MS  1000U   // while awake: the state's blips once a second (1 off, 2 idle, 3 operating)
#define LED_UNSET_HALF_MS    100U    // clock unset: 100 ms on / 100 ms off (~0.15 mA lit through 4.7k)
#define LED_WAKE_MS          50U     // flash on each wake
#define AWAKE_MS             10000U  // stay up after a wake for button presses and serial commands
#define ACC_WAKE_EN          true    // true: motion on INT1 wakes from deep sleep (state off); false: button only
#define HUM_EN               false   // true: also run bracket detection all through the awake window, with hum:
                                     // lines and per-frame blips (bench); the state machine measures either way
#define DBG_EN               true    // true: test timescale and DBG lines for each sample, sleep and wake
                                     // (field testing): 5 s light sleep, status totals in seconds
#define SM_LIGHT_SLEEP_S     (DBG_EN ? 5U : 60U)  // light sleep (idle, operating): the RTC timer wakes it
                                                  // this often to measure
#define HUM_MAX_FRAMES       5U      // a measurement stops once 2 frames agree, or after this many (3.2 s)
#define I2C_CLOCK_HZ         400000U // fast mode: drains the accel FIFO in time at 800 Hz
#define RX_LINE_MAX          32U

// LIS2DTW12 registers (datasheet DS12825 section 8)
#define ACC_REG_OUT_T_L      0x0DU   // temperature, 12-bit LE left-justified, 16 LSB/C, 0 = 25 C
#define ACC_REG_WHO_AM_I     0x0FU   // reads 0x44
#define ACC_REG_CTRL1        0x20U   // ODR[7:4], MODE[3:2], LP_MODE[1:0]
#define ACC_REG_CTRL2        0x21U   // BOOT, SOFT_RESET, -, CS_PU_DISC, BDU, IF_ADD_INC, I2C_DISABLE, SIM
#define ACC_REG_CTRL3        0x22U   // ST[7:6], PP_OD, LIR, H_LACTIVE, -, SLP_MODE_SEL, SLP_MODE_1
#define ACC_REG_CTRL4        0x23U   // INT1 pad routing, bit 5 INT1_WU
#define ACC_REG_CTRL6        0x25U   // BW_FILT[7:6], FS[5:4], FDS, LOW_NOISE
#define ACC_REG_OUT_X_L      0x28U   // X, Y, Z as 16-bit LE, left-justified; FIFO head when FIFO is on
#define ACC_REG_FIFO_CTRL    0x2EU   // FMode[7:5], FTH[4:0]
#define ACC_REG_FIFO_SAMPLES 0x2FU   // FIFO_FTH, FIFO_OVR, Diff[5:0] unread samples
#define ACC_REG_WAKE_UP_THS  0x34U   // SINGLE_DOUBLE_TAP, SLEEP_ON, WK_THS[5:0]
#define ACC_REG_ALL_INT_SRC  0x3BU   // reading clears latched interrupts
#define ACC_REG_CTRL7        0x3FU   // DRDY_PULSED, INT2_ON_INT1, INTERRUPTS_ENABLE, ...

#define ACC_WHO_AM_I_VAL     0x44U
#define ACC_CTRL1_POWER_DOWN 0x00U   // ODR off (~50 nA)
#define ACC_CTRL1_12HZ5_LP1  0x20U   // ODR 12.5 Hz, low-power mode 1 (12-bit, ~1 uA)
#define ACC_CTRL1_800HZ_HP   0x84U   // ODR 800 Hz, high-performance (14-bit, ~90 uA)
#define ACC_CTRL2_SOFT_RESET 0x40U
#define ACC_CTRL2_BDU_INC    0x0CU   // block data update, address auto-increment
#define ACC_CTRL3_LIR        0x10U   // latch INT1 until ALL_INT_SRC is read
#define ACC_CTRL4_INT1_WU    0x20U
#define ACC_CTRL6_2G         0x00U   // +/-2 g, ODR/2 bandwidth, low-noise off
#define ACC_CTRL6_HUM        0x64U   // ODR/4 bandwidth (200 Hz at 800 Hz), +/-8 g, low-noise on
#define ACC_CTRL7_INT_EN     0x20U   // INTERRUPTS_ENABLE
#define ACC_FIFO_BYPASS      0x00U   // FIFO off (and emptied)
#define ACC_FIFO_CONTINUOUS  0xC0U   // newest 32 samples, oldest overwritten when full
#define ACC_FIFO_OVR         0x40U
#define ACC_FIFO_DIFF        0x3FU
#define ACC_FIFO_CHUNK       16U     // samples per I2C burst (96 bytes)
#define ACC_WU_THS           2U      // wake on motion over 2 x 31.25 mg (1/64 of FS), high-pass filtered
#define ACC_INT_DROP_MS      200U    // INT1 drops on the next internal sample after a clear (80 ms at 12.5 Hz)
#define ACC_SETTLE_MS        250U    // after leaving hum mode, let the wake-up filter settle before sleeping
#define ACC_UG_PER_LSB_HUM   976     // +/-8 g: 0.976 mg per 14-bit LSB (first machine test peaked at 4 g)
#define ACC_FULL_SCALE       8191    // 14-bit max count: a sample at this level has clipped
#define HUM_FIRST_MS         20U     // after hum_start, ~16 samples (FIFO holds 32) and a fresh temperature
                                     // before the wake status line

// engine hum brackets (docs/hum_engine_freq.md): Goertzel over 128-sample blocks at 800 Hz,
// bins 5..24 = 31.25..150 Hz in 6.25 Hz steps, all three axes; a frame is 4 blocks (512 samples, 0.64 s)
#define HUM_N                128U
#define HUM_BLOCKS           4U
#define HUM_BIN_FIRST        5U      // 31.25 Hz
#define HUM_NBINS            20U     // through bin 24, 150 Hz
#define HUM_BIN_IDLE_HI      9U      // 31.25..56.25 Hz: idle, 30-55 Hz (first machine test idled at 37.5-43.75)
                                     // 62.5..150 Hz: operating, 65-150 Hz (62.5 is nearer operating)
// amplitude gate: the winning band's rms must reach a line from 700 mg at 30 Hz down to 200 mg at 100 Hz
// (flat outside), set at the frequency of that band's strongest bin; below it reads as off
#define HUM_GATE_LO_HZ       30U
#define HUM_GATE_LO_DMG      7000U   // tenths of a mg
#define HUM_GATE_HI_HZ       100U
#define HUM_GATE_HI_DMG      2000U
#define HUM_CONFIRM          2U      // consecutive frames that must agree before the bracket changes
#define HUM_BRACKET_NONE     0U      // no two frames have agreed yet since the wake
#define HUM_BRACKET_OFF      1U
#define HUM_BRACKET_IDLE     2U
#define HUM_BRACKET_OP       3U

// 24FC512: 64 KB, 2-byte address, 128-byte pages, 5 ms max write cycle, WP = GND
// no sleep command: it drops to standby (1 uA max) whenever the bus is idle and no write is running
#define EEP_SIZE             65536UL
#define EEP_PAGE_SIZE        128U
#define EEP_ERASE_CONFIRM_MS 10000U  // after the ERASE command, a button press within this long erases
#define EEP_WRITE_TIMEOUT_MS 10U
// EEPROM map: each counter is a uint32_t LE, 0xFFFFFFFF when erased (eep_read_count reads that as 0)
#define EEP_ADDR_UNIT        0x0000U  // unit number (serial number), set over UART with U<number>
#define EEP_ADDR_BOOT_COUNT  0x0004U  // boots
#define EEP_ADDR_IDLE_S      0x0008U  // total idle duration, seconds (docs/state_machine.md)
#define EEP_ADDR_OP_S        0x000CU  // total operating duration, seconds
#define EEP_ADDR_RTC_CAL     0x0010U  // RTC drift being corrected, int32 in 0.1 ppm, + = runs fast; C<ppm>
#define EEP_UNIT_UNSET       0xFFFFFFFFUL
#define RTC_CAL_MAX_TENTHS   4800    // +-480.0 ppm, inside smooth calibration's -487.1..+488.5 ppm


// the variant's default Serial is USART4 on PA0/PA1 (ACC_INT1/BTN), so use USART1
Uart uart(UART_RX_PIN, UART_TX_PIN);

static RTC_HandleTypeDef hrtc;

static bool lse_ok = false;
static bool rtc_ok = false;
static bool acc_ok = false;
static bool eep_ok = false;
static uint32_t unit_number = EEP_UNIT_UNSET;
static int32_t rtc_cal_tenths = 0;          // RTC drift being corrected, 0.1 ppm, + = runs fast
static volatile bool btn_woke = false;      // set by the PA1 / PA0 EXTI handlers
static volatile bool acc_woke = false;
static volatile bool rtc_woke = false;      // set by the RTC wake-up timer
static uint8_t sm_state = HUM_BRACKET_OFF;  // state machine (docs/state_machine.md): off, idle or operating
static uint32_t sm_since_s = 0;             // when the current state began, RTC seconds since 2000
static bool btn_down = false;               // debounced button state
static uint32_t btn_stable_ms = 0;
static bool eep_erase_armed = false;        // ERASE received, waiting for the button to accept
static uint32_t eep_erase_ms = 0;           // when it was received
static uint8_t led_blips = 0;               // blips still to show in the current pattern
static uint32_t led_next_ms = 0;            // when the next blip starts

// 2 cos(2 pi k / 128) in Q14, k = 5..24
static const int16_t hum_coeff[HUM_NBINS] = {
  31786, 31357, 30853, 30274, 29622, 28899, 28106, 27246, 26320, 25330,
  24279, 23170, 22006, 20788, 19520, 18205, 16846, 15447, 14010, 12540
};
static bool hum_on = false;
static bool hum_seeded = false;             // hum_dc holds a real sample since hum_start
static int32_t hum_s1[3][HUM_NBINS], hum_s2[3][HUM_NBINS];   // Goertzel state per axis and bin
static int32_t hum_dc[3], hum_sum[3];       // previous frame mean per axis, and this frame's running sum
static uint64_t hum_pow[HUM_NBINS];         // bin power over the frame, summed across axes
static uint16_t hum_n = 0;                  // samples into the current block
static uint8_t hum_block = 0;               // blocks into the current frame
static uint8_t hum_state = HUM_BRACKET_NONE;  // confirmed bracket
static uint8_t hum_cand = HUM_BRACKET_NONE; // bracket waiting for confirmation, and its run of frames
static uint8_t hum_cand_n = 0;
static int32_t hum_peak = 0;                // largest |count| this frame, gravity included
static bool hum_ovr = false;                // FIFO overran this frame: samples were lost
static int16_t hum_last[3];                 // newest sample drained from the FIFO
static bool hum_measuring = false;          // sm_measure is running: analyse frames even with HUM_EN off
static uint8_t hum_frames = 0;              // frames finished since hum_start
static const char *const hum_names[] = {"", "off", "idle", "operating"};


// replaces the variant's empty weak default; runs before setup() and again after each Stop 2 wake
// SYSCLK = MSI 16 MHz, voltage range 2 (max 16 MHz, 1 WS), MSI trimmed by LSE (MSI PLL mode)
extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {};
  RCC_ClkInitTypeDef clk = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2);

  // LSE: 32.768 kHz crystal on PC14/PC15; BDCR needs backup domain write access
  // medium-low drive for bring-up; try RCC_LSEDRIVE_LOW once startup is proven on hardware
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMLOW);

  osc.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
  osc.LSEState = RCC_LSE_ON;
  osc.MSIState = RCC_MSI_ON;
  osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  osc.MSIClockRange = RCC_MSIRANGE_8;    // 16 MHz
  osc.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
    lse_ok = true;
  } else {
    // LSE didn't start (5 s timeout): run MSI untrimmed so bring-up can continue
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
      Error_Handler();
    }
  }

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) {
    Error_Handler();
  }

  if (lse_ok) {
    HAL_RCCEx_EnableMSIPLLMode();
  }
}


static bool acc_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission((uint8_t)ACC_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool acc_read(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission((uint8_t)ACC_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)ACC_I2C_ADDR, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

// low-power mode 1 at 12.5 Hz, with a latched wake-up (motion) interrupt on INT1
static bool acc_init(void) {
  uint8_t val = 0;

  if (!acc_read(ACC_REG_WHO_AM_I, &val, 1) || val != ACC_WHO_AM_I_VAL) {
    uart.printf("acc: WHO_AM_I 0x%02X, expected 0x%02X\r\n", val, ACC_WHO_AM_I_VAL);
    return false;
  }

  // soft reset clears itself when done
  acc_write(ACC_REG_CTRL2, ACC_CTRL2_SOFT_RESET);
  do {
    delay(1);
    if (!acc_read(ACC_REG_CTRL2, &val, 1)) {
      return false;
    }
  } while (val & ACC_CTRL2_SOFT_RESET);

  return acc_write(ACC_REG_CTRL2, ACC_CTRL2_BDU_INC) &&
         acc_write(ACC_REG_CTRL3, ACC_CTRL3_LIR) &&
         acc_write(ACC_REG_CTRL6, ACC_CTRL6_2G) &&
         acc_write(ACC_REG_WAKE_UP_THS, ACC_WU_THS) &&
         acc_write(ACC_REG_CTRL4, ACC_CTRL4_INT1_WU) &&
         acc_write(ACC_REG_CTRL7, ACC_CTRL7_INT_EN) &&
         acc_write(ACC_REG_CTRL1, ACC_CTRL1_12HZ5_LP1);
}

// INT1 stays high until this read (and up to one sample after), and only a fresh rising edge
// can wake the MCU
static void acc_clear_int(void) {
  uint8_t src;

  acc_read(ACC_REG_ALL_INT_SRC, &src, 1);
}

// die temperature in tenths of a degree C; +/-0.8 C typ over 0..70 C
static bool acc_read_temp_dc(int32_t *dc) {
  uint8_t buf[2];

  if (!acc_read(ACC_REG_OUT_T_L, buf, sizeof(buf))) {
    return false;
  }
  *dc = 250 + ((int16_t)(buf[1] << 8 | buf[0]) >> 4) * 10 / 16;
  return true;
}


// ACK polling: the device NAKs its address until the internal write cycle ends
static bool eep_wait_ready(void) {
  uint32_t start = millis();

  do {
    Wire.beginTransmission((uint8_t)EEPROM_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
      return true;
    }
  } while (millis() - start < EEP_WRITE_TIMEOUT_MS);
  return false;
}

static bool eep_read(uint16_t addr, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission((uint8_t)EEPROM_I2C_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)addr);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)EEPROM_I2C_ADDR, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

// writes must not cross a page boundary or they wrap within the page
static bool eep_write(uint16_t addr, const uint8_t *buf, uint8_t len) {
  if (len == 0 || len > EEP_PAGE_SIZE - addr % EEP_PAGE_SIZE) {
    return false;
  }
  Wire.beginTransmission((uint8_t)EEPROM_I2C_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)addr);
  Wire.write(buf, len);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  return eep_wait_ready();
}

// counters read as 0 when erased (0xFFFFFFFF)
static bool eep_read_count(uint16_t addr, uint32_t *count) {
  if (!eep_read(addr, (uint8_t *)count, sizeof(*count))) {
    return false;
  }
  if (*count == 0xFFFFFFFFUL) {
    *count = 0;
  }
  return true;
}

static bool eep_write_count(uint16_t addr, uint32_t count) {
  uint32_t check = 0;

  return eep_write(addr, (const uint8_t *)&count, sizeof(count)) &&
         eep_read(addr, (uint8_t *)&check, sizeof(check)) && check == count;
}

// "unset" or the unit number in decimal
static void unit_format(char *buf, size_t len) {
  if (unit_number == EEP_UNIT_UNSET) {
    snprintf(buf, len, "unset");
  } else {
    snprintf(buf, len, "%lu", unit_number);
  }
}

// probe the device, read the unit number and bump the boot counter
static bool eep_init(void) {
  uint32_t boots;
  char unit[12];

  if (!eep_wait_ready()) {
    uart.println("eep: no ACK at 0x50");
    return false;
  }
  if (!eep_read(EEP_ADDR_UNIT, (uint8_t *)&unit_number, sizeof(unit_number))) {
    uart.println("eep: unit number read failed");
    return false;
  }
  unit_format(unit, sizeof(unit));
  uart.printf("unit: %s%s\r\n", unit, unit_number == EEP_UNIT_UNSET ? ", send U<number>" : "");
  if (!eep_read_count(EEP_ADDR_BOOT_COUNT, &boots) ||
      !eep_write_count(EEP_ADDR_BOOT_COUNT, ++boots)) {
    uart.println("eep: boot count read/write failed");
    return false;
  }
  uart.printf("eep: boot count %lu\r\n", boots);
  return true;
}

// back to the erased state (all 0xFF), page by page with read-back; 5.4 s for 64 KB
static bool eep_erase(void) {
  uint8_t buf[EEP_PAGE_SIZE];

  for (uint32_t addr = 0; addr < EEP_SIZE; addr += EEP_PAGE_SIZE) {
    memset(buf, 0xFF, sizeof(buf));
    if (!eep_write(addr, buf, sizeof(buf)) || !eep_read(addr, buf, sizeof(buf))) {
      uart.printf("eep: erase failed at 0x%04lX\r\n", addr);
      return false;
    }
    for (uint8_t i = 0; i < sizeof(buf); i++) {
      if (buf[i] != 0xFF) {
        uart.printf("eep: erase verify failed at 0x%04lX\r\n", addr + i);
        return false;
      }
    }
  }
  return true;
}


// VDDA = VCC = coin cell (R2 = 0R, no diode); VREFINT is factory-calibrated at VDDA = 3.0 V
static uint32_t vcc_read_mv(void) {
  uint32_t raw = analogRead(AVREF);

  return raw ? __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw, LL_ADC_RESOLUTION_12B) : 0;
}


// RTC on LSE, 1 Hz = 32768 / (127 + 1) / (255 + 1); calendar and prescalers live in the backup domain
static bool rtc_init(void) {
  RCC_PeriphCLKInitTypeDef clk = {};

  if (!lse_ok) {
    return false;
  }
  // only resets the backup domain if a different RTC clock was already selected
  clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  clk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
    return false;
  }
  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTCAPB_CLK_ENABLE();

  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
  hrtc.Init.BinMode = RTC_BINARY_NONE;
  hrtc.Init.BinMixBcdU = RTC_BINARY_MIX_BCDU_0;

  // re-initializing a running clock restarts the prescalers and loses up to 1 s, so skip it once set
  if (!(RTC->ICSR & RTC_ICSR_INITS)) {
    return HAL_RTC_Init(&hrtc) == HAL_OK;
  }
  // shadow registers are stale after reset until RSF sets (2 RTCCLK)
  uint32_t start = millis();
  while (!(RTC->ICSR & RTC_ICSR_RSF)) {
    if (millis() - start > 10) {
      return false;
    }
  }
  return true;
}

// smooth calibration over 32 s (2^20 RTCCLK): CALM masks that many pulses (slower), CALP adds 512
// (faster); one pulse is 1e6 / 2^20 = 0.954 ppm. CALR is in the backup domain, so it survives resets
static bool rtc_cal_apply(int32_t tenths) {
  uint32_t mag = (uint32_t)(tenths < 0 ? -tenths : tenths);
  // pulses = ppm * 2^20 / 1e6 = tenths * 65536 / 625000, rounded; fits 32 bits for |tenths| <= 4800
  uint32_t pulses = (mag * 65536U + 312500U) / 625000U;
  uint32_t plus = RTC_SMOOTHCALIB_PLUSPULSES_RESET, minus = pulses;

  if (tenths < 0 && pulses > 0) {   // runs slow: add 512 pulses and mask all but the shortfall
    plus = RTC_SMOOTHCALIB_PLUSPULSES_SET;
    minus = 512U - pulses;
  }
  if (!rtc_ok || minus > RTC_CALR_CALM_Msk) {
    return false;
  }
  if ((RTC->CALR & (RTC_CALR_CALP | RTC_CALR_CALW8 | RTC_CALR_CALW16 | RTC_CALR_CALM)) == (plus | minus)) {
    return true;                    // already set (kept through the reset): skip the write
  }
  return HAL_RTCEx_SetSmoothCalib(&hrtc, RTC_SMOOTHCALIB_PERIOD_32SEC, plus, minus) == HAL_OK;
}

// "+51.8 ppm"
static void rtc_cal_format(char *buf, size_t len) {
  uint32_t mag = (uint32_t)(rtc_cal_tenths < 0 ? -rtc_cal_tenths : rtc_cal_tenths);

  snprintf(buf, len, "%c%lu.%lu ppm", rtc_cal_tenths < 0 ? '-' : '+', mag / 10, mag % 10);
}

// apply the stored drift; erased (0xFFFFFFFF) reads as 0, no correction
static void rtc_cal_load(void) {
  uint32_t raw;
  char cal[16];

  if (!eep_ok || !eep_read_count(EEP_ADDR_RTC_CAL, &raw)) {
    uart.println("rtc: cal unread, CALR left as is");
    return;
  }
  rtc_cal_tenths = (int32_t)raw;
  if (rtc_cal_tenths > RTC_CAL_MAX_TENTHS || rtc_cal_tenths < -RTC_CAL_MAX_TENTHS) {
    uart.printf("rtc: cal %ld out of range, ignored\r\n", rtc_cal_tenths);
    rtc_cal_tenths = 0;
  }
  rtc_cal_format(cal, sizeof(cal));
  uart.printf("rtc: cal %s%s\r\n", cal, rtc_cal_apply(rtc_cal_tenths) ? "" : ", CALR write failed");
}

// INITS sets once a date is written and clears on backup domain reset (power loss)
static bool rtc_is_set(void) {
  return rtc_ok && (RTC->ICSR & RTC_ICSR_INITS);
}

// YYYYMMDD-HH:MM:SS, 24 h; buf needs 18 bytes
static bool rtc_format(char *buf, size_t len) {
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;

  if (!rtc_is_set()) {
    return false;
  }
  // GetDate must follow GetTime to unlock the shadow registers
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  snprintf(buf, len, "%04u%02u%02u-%02u:%02u:%02u", 2000U + d.Year, d.Month, d.Date,
           t.Hours, t.Minutes, t.Seconds);
  return true;
}

// seconds since 2000-01-01 00:00:00; every 4th year from 2000 to 2099 is a leap year
static bool rtc_now_s(uint32_t *s) {
  static const uint16_t cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;

  if (!rtc_is_set()) {
    return false;
  }
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  uint32_t days = 365UL * d.Year + (d.Year + 3U) / 4U + cum[d.Month - 1] + d.Date - 1U +
                  (d.Year % 4U == 0 && d.Month > 2 ? 1U : 0U);
  *s = days * 86400UL + t.Hours * 3600UL + t.Minutes * 60UL + t.Seconds;
  return true;
}

static int parse_num(const char *s, int digits) {
  int n = 0;

  for (int i = 0; i < digits; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return -1;
    }
    n = n * 10 + (s[i] - '0');
  }
  return n;
}

// sets the clock from YYYYMMDD-HH:MM:SS
static bool rtc_set(const char *s) {
  static const uint8_t mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  RTC_TimeTypeDef t = {};
  RTC_DateTypeDef d = {};

  if (!rtc_ok || strlen(s) != 17 || s[8] != '-' || s[11] != ':' || s[14] != ':') {
    return false;
  }
  int year = parse_num(s, 4), mon = parse_num(s + 4, 2), day = parse_num(s + 6, 2);
  int hour = parse_num(s + 9, 2), min = parse_num(s + 12, 2), sec = parse_num(s + 15, 2);
  // year 2000 would leave INITS clear, which reads as unset
  if (year < 2001 || year > 2099 || mon < 1 || mon > 12 || day < 1 || day > mdays[mon - 1] ||
      (mon == 2 && day == 29 && year % 4 != 0) || hour < 0 || hour > 23 || min < 0 || min > 59 ||
      sec < 0 || sec > 59) {
    return false;
  }

  // Sakamoto's day of week, 0 = Sunday; the RTC wants Monday = 1 .. Sunday = 7
  static const uint8_t mt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year - (mon < 3);
  int wd = (y + y / 4 - y / 100 + y / 400 + mt[mon - 1] + day) % 7;

  t.Hours = hour;
  t.Minutes = min;
  t.Seconds = sec;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  d.Year = year - 2000;
  d.Month = mon;
  d.Date = day;
  d.WeekDay = wd ? wd : 7;
  return HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) == HAL_OK &&
         HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) == HAL_OK;
}


static uint32_t isqrt64(uint64_t v) {
  uint64_t r = 0, bit = 1ULL << 62;

  while (bit > v) {
    bit >>= 2;
  }
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)r;
}

// rms in tenths of a mg for frame bin power p: a sine of amplitude A counts gives |X|^2 = (A N / 2)^2,
// so rms = sqrt(2 |X|^2) / N counts, averaged over the frame's blocks
static uint32_t hum_dmg(uint64_t p) {
  return (uint64_t)isqrt64(2 * p / HUM_BLOCKS) * ACC_UG_PER_LSB_HUM / (HUM_N * 100);
}

static void hum_reset(void) {
  memset(hum_s1, 0, sizeof(hum_s1));
  memset(hum_s2, 0, sizeof(hum_s2));
  memset(hum_pow, 0, sizeof(hum_pow));
  memset(hum_sum, 0, sizeof(hum_sum));
  hum_n = 0;
  hum_block = 0;
  hum_peak = 0;
  hum_ovr = false;
}

// gate level in tenths of a mg at f_x100 (hundredths of a Hz)
static uint32_t hum_gate_dmg(uint32_t f_x100) {
  if (f_x100 < HUM_GATE_LO_HZ * 100) {
    f_x100 = HUM_GATE_LO_HZ * 100;
  } else if (f_x100 > HUM_GATE_HI_HZ * 100) {
    f_x100 = HUM_GATE_HI_HZ * 100;
  }
  return HUM_GATE_LO_DMG - (f_x100 - HUM_GATE_LO_HZ * 100) * (HUM_GATE_LO_DMG - HUM_GATE_HI_DMG) /
                           ((HUM_GATE_HI_HZ - HUM_GATE_LO_HZ) * 100);
}

// the bracket only changes after HUM_CONFIRM frames in a row agree on a new one
static void hum_confirm(uint8_t raw) {
  if (raw == hum_state) {
    hum_cand_n = 0;
    return;
  }
  if (raw != hum_cand) {
    hum_cand = raw;
    hum_cand_n = 0;
  }
  if (++hum_cand_n >= HUM_CONFIRM) {
    hum_state = raw;
    hum_cand_n = 0;
  }
}

// frame done: the stronger band picks idle or operating if it clears the gate, else off; confirm it,
// print it, queue the confirmed bracket's blips
static void hum_frame(void) {
  uint64_t idle = 0, op = 0, idle_best = 0, op_best = 0;
  uint8_t idle_bin = 0, op_bin = 0;

  for (uint8_t b = 0; b < HUM_NBINS; b++) {
    uint8_t k = HUM_BIN_FIRST + b;

    if (k <= HUM_BIN_IDLE_HI) {
      idle += hum_pow[b];
      if (hum_pow[b] > idle_best) {
        idle_best = hum_pow[b];
        idle_bin = k;
      }
    } else {
      op += hum_pow[b];
      if (hum_pow[b] > op_best) {
        op_best = hum_pow[b];
        op_bin = k;
      }
    }
  }

  bool idle_wins = idle > op;
  uint32_t f_x100 = (idle_wins ? idle_bin : op_bin) * 80000U / HUM_N;   // bin k is k * 800 / 128 Hz
  uint32_t idle_dmg = hum_dmg(idle), op_dmg = hum_dmg(op), total_dmg = hum_dmg(idle + op);
  uint32_t gate_dmg = hum_gate_dmg(f_x100);
  uint8_t raw = HUM_BRACKET_OFF;
  if ((idle_wins ? idle_dmg : op_dmg) >= gate_dmg) {
    raw = idle_wins ? HUM_BRACKET_IDLE : HUM_BRACKET_OP;
  }
  hum_confirm(raw);
  hum_frames++;

  if (HUM_EN) {
    // until two frames agree there's nothing confirmed, so show what this frame sensed
    uint8_t shown = hum_state != HUM_BRACKET_NONE ? hum_state : raw;
    uint32_t peak_mg = (uint32_t)hum_peak * ACC_UG_PER_LSB_HUM / 1000;

    uart.printf("hum: %u %-9s  raw %u  f %3lu.%02lu Hz  idle %lu.%lu  op %lu.%lu  all %lu.%lu  gate %lu.%lu mg rms"
                "  peak %lu mg%s%s\r\n",
                shown, hum_names[shown], raw, f_x100 / 100, f_x100 % 100, idle_dmg / 10, idle_dmg % 10,
                op_dmg / 10, op_dmg % 10, total_dmg / 10, total_dmg % 10, gate_dmg / 10, gate_dmg % 10, peak_mg,
                hum_peak >= ACC_FULL_SCALE ? "  CLIPPED" : "", hum_ovr ? "  OVERRUN" : "");

    // while the clock is unset its rapid flash owns the LED
    if (rtc_is_set()) {
      led_blips = shown;
      led_next_ms = millis();
    }
  }
  memset(hum_pow, 0, sizeof(hum_pow));
  hum_block = 0;
  hum_peak = 0;
  hum_ovr = false;
}

// one 800 Hz sample (14-bit counts per axis) through every bin's Goertzel filter
static void hum_sample(const int16_t v[3]) {
  if (!hum_seeded) {
    for (uint8_t a = 0; a < 3; a++) {
      hum_dc[a] = v[a];
    }
    hum_seeded = true;
  }
  for (uint8_t a = 0; a < 3; a++) {
    int32_t abs_v = v[a] < 0 ? -v[a] : v[a];
    if (abs_v > hum_peak) {
      hum_peak = abs_v;
    }
    hum_sum[a] += v[a];
    // removing the previous frame's mean keeps gravity out of the fixed-point state; it's
    // constant across every block, so it doesn't leak into the bins
    int32_t x = v[a] - hum_dc[a];
    for (uint8_t b = 0; b < HUM_NBINS; b++) {
      int32_t s0 = x + (int32_t)(((int64_t)hum_coeff[b] * hum_s1[a][b]) >> 14) - hum_s2[a][b];
      hum_s2[a][b] = hum_s1[a][b];
      hum_s1[a][b] = s0;
    }
  }
  if (++hum_n < HUM_N) {
    return;
  }

  // end of block: |X_k|^2 = s1^2 + s2^2 - coeff s1 s2
  for (uint8_t a = 0; a < 3; a++) {
    for (uint8_t b = 0; b < HUM_NBINS; b++) {
      int64_t s1 = hum_s1[a][b], s2 = hum_s2[a][b];
      int64_t p = s1 * s1 + s2 * s2 - (((int64_t)hum_coeff[b] * s1) >> 14) * s2;
      hum_pow[b] += p > 0 ? p : 0;
      hum_s1[a][b] = 0;
      hum_s2[a][b] = 0;
    }
  }
  hum_n = 0;
  if (++hum_block < HUM_BLOCKS) {
    return;
  }

  for (uint8_t a = 0; a < 3; a++) {
    hum_dc[a] = hum_sum[a] / (int32_t)(HUM_N * HUM_BLOCKS);
    hum_sum[a] = 0;
  }
  hum_frame();
}

// drain the FIFO; at 800 Hz it holds 40 ms, so this must run at least that often. The bracket
// analysis runs during a measurement or with HUM_EN; otherwise the samples are just discarded
static void hum_service(void) {
  uint8_t st, buf[ACC_FIFO_CHUNK * 6];

  if (!hum_on || !acc_read(ACC_REG_FIFO_SAMPLES, &st, 1)) {
    return;
  }
  if (st & ACC_FIFO_OVR) {
    hum_ovr = true;
  }
  uint8_t n = st & ACC_FIFO_DIFF;
  while (n > 0) {
    uint8_t chunk = n < ACC_FIFO_CHUNK ? n : ACC_FIFO_CHUNK;
    // reads past 0x2D wrap to 0x28, so one burst pops several samples
    if (!acc_read(ACC_REG_OUT_X_L, buf, chunk * 6)) {
      return;
    }
    for (uint8_t i = 0; i < chunk; i++) {
      for (uint8_t a = 0; a < 3; a++) {
        hum_last[a] = (int16_t)(buf[i * 6 + a * 2 + 1] << 8 | buf[i * 6 + a * 2]) >> 2;
      }
      if (HUM_EN || hum_measuring) {
        hum_sample(hum_last);
      }
    }
    n -= chunk;
  }
}

// awake: 800 Hz high-performance capture into the FIFO, motion interrupt off
static void hum_start(void) {
  if (!acc_ok) {
    return;
  }
  acc_write(ACC_REG_CTRL4, 0);
  acc_write(ACC_REG_FIFO_CTRL, ACC_FIFO_BYPASS);
  acc_write(ACC_REG_CTRL6, ACC_CTRL6_HUM);
  acc_write(ACC_REG_CTRL1, ACC_CTRL1_800HZ_HP);
  acc_write(ACC_REG_FIFO_CTRL, ACC_FIFO_CONTINUOUS);
  hum_reset();
  hum_seeded = false;
  hum_state = HUM_BRACKET_NONE;   // each measurement decides from its own frames, not a default
  hum_cand = HUM_BRACKET_NONE;
  hum_cand_n = 0;
  hum_on = true;
}

// leave capture: low-power motion wake-up on INT1 (deep sleep), or powered down (light sleep)
static void hum_stop(bool motion) {
  if (!hum_on) {
    return;
  }
  hum_on = false;
  acc_write(ACC_REG_FIFO_CTRL, ACC_FIFO_BYPASS);
  if (!motion) {
    acc_write(ACC_REG_CTRL1, ACC_CTRL1_POWER_DOWN);
    return;
  }
  acc_write(ACC_REG_CTRL1, ACC_CTRL1_12HZ5_LP1);
  acc_write(ACC_REG_CTRL6, ACC_CTRL6_2G);
  acc_write(ACC_REG_CTRL4, ACC_CTRL4_INT1_WU);
  delay(ACC_SETTLE_MS);
}


static void btn_isr(void) {
  btn_woke = true;
}

static void acc_isr(void) {
  acc_woke = true;
}

extern "C" void RTC_TAMP_IRQHandler(void) {
  HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}

extern "C" void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *h) {
  (void)h;
  rtc_woke = true;
}

// Stop 2 until the button, motion or the RTC timer: RTC, LSE and the EXTI wake pins keep running,
// SRAM is kept, everything else stops.
// SWD drops out too, so uploads connect under reset (see upload_flags in platformio.ini).
static void mcu_sleep(void) {
  acc_clear_int();
  // wait for INT1 to actually drop; if it doesn't, motion is ongoing and the loop below wakes at once
  uint32_t start = millis();
  while (digitalRead(ACC_INT1_PIN) == HIGH && millis() - start < ACC_INT_DROP_MS) {
  }
  // an event latched before the clear is stale (e.g. the step from capture to motion mode); motion
  // from here on raises a new edge, or re-latches INT1 for the level check below
  acc_woke = false;
  uart.flush();

  // keep the flash powered in Stop 2: with it powered down (the reset default) an RTC timer wake
  // HardFaults fetching the first instruction after WFI; button wakes were unaffected
  CLEAR_BIT(PWR->CR1, PWR_CR1_FPD_STOP);
  // stop SysTick before masking, or a tick left pending ends WFI at once
  HAL_SuspendTick();
  // with interrupts masked, an edge that lands after a check still ends WFI immediately;
  // unmasking briefly after each wake lets the EXTI handler set its flag
  __disable_irq();
  while (!btn_woke && !acc_woke && !rtc_woke) {
    if (digitalRead(ACC_INT1_PIN) == HIGH) {
      acc_woke = true;    // re-latched, so no rising edge is coming
      break;
    }
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
    __enable_irq();
    __disable_irq();
  }
  __enable_irq();
  HAL_ResumeTick();

  // wakes on MSI; restore LSE trim and the 16 MHz clock tree
  SystemClock_Config();
}


void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(ACC_INT1_PIN, INPUT);
  analogReadResolution(12);
  uart.begin(UART_BAUD);

  // the variant's default Wire is PA6/PA7, so move it to I2C1 on PB6/PB7
  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);

  uart.println("\r\nhum_fw " FW_REV);    // blank line first, to set each boot apart in a running log
  uart.println(lse_ok ? "lse: ok" : "lse: FAIL, MSI untrimmed");
  rtc_ok = rtc_init();
  char ts[18];
  if (!rtc_ok) {
    uart.println("rtc: FAIL");
  } else if (rtc_format(ts, sizeof(ts))) {
    uart.printf("rtc: %s\r\n", ts);
  } else {
    uart.println("rtc: unset, send TYYYYMMDD-HH:MM:SS (tools/set_rtc.py)");
  }
  uart.printf("vcc: %lu mV\r\n", vcc_read_mv());
  acc_ok = acc_init();
  uart.println(acc_ok ? "acc: ok" : "acc: init failed");
  eep_ok = eep_init();
  uart.println(eep_ok ? "eep: ok" : "eep: FAIL");
  if (rtc_ok) {
    rtc_cal_load();
  }

  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btn_isr, FALLING);
  HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(RTC_TAMP_IRQn);
  if (ACC_WAKE_EN) {
    attachInterrupt(digitalPinToInterrupt(ACC_INT1_PIN), acc_isr, RISING);
  }
  hum_start();
}

// true once per press: the pin must read the new level for BTN_DEBOUNCE_MS before it counts
static bool btn_pressed(void) {
  bool low = digitalRead(BTN_PIN) == LOW;

  if (low == btn_down) {
    btn_stable_ms = millis();
  } else if (millis() - btn_stable_ms >= BTN_DEBOUNCE_MS) {
    btn_down = low;
    btn_stable_ms = millis();
    return btn_down;
  }
  return false;
}

// LED patterns: clock unset = rapid 50% flash; otherwise blips for the state, 1 off, 2 idle, 3 operating,
// once a second (with HUM_EN, once per hum frame instead)
// (swap for Flasher's portable branch once it's public)
static void led_service(void) {
  static bool lit = false, unset = false;
  static uint32_t lit_ms = 0, state_ms = 0;
  uint32_t now = millis();

  if (!rtc_is_set()) {
    digitalWrite(LED_PIN, now % (2 * LED_UNSET_HALF_MS) < LED_UNSET_HALF_MS ? HIGH : LOW);
    unset = true;
    return;
  }
  if (unset) {                    // clock just set: don't leave the flash lit
    digitalWrite(LED_PIN, LOW);
    unset = false;
    lit = false;
  }
  if (!HUM_EN && now - state_ms >= LED_STATE_PERIOD_MS) {
    state_ms = now;
    led_blips = sm_state;
    led_next_ms = now;
  }
  // timed from when the blip actually started, so a slow loop pass stretches it rather than skipping it
  if (lit) {
    if (now - lit_ms >= LED_BLIP_ON_MS) {
      digitalWrite(LED_PIN, LOW);
      lit = false;
      led_next_ms = lit_ms + LED_BLIP_GAP_MS;
    }
  } else if (led_blips > 0 && (int32_t)(now - led_next_ms) >= 0) {
    digitalWrite(LED_PIN, HIGH);
    lit = true;
    lit_ms = now;
    led_blips--;
  }
}

// U<number>: decimal digits only, stored with read-back
static void unit_set(const char *s) {
  char *end;
  char unit[12];

  if (*s < '0' || *s > '9' || strlen(s) > 10) {
    uart.printf("unit: bad number '%s'\r\n", s);
    return;
  }
  unsigned long long v = strtoull(s, &end, 10);
  if (*end != '\0' || v >= EEP_UNIT_UNSET) {
    uart.printf("unit: bad number '%s'\r\n", s);
    return;
  }
  if (!eep_ok || !eep_write_count(EEP_ADDR_UNIT, (uint32_t)v)) {
    uart.println("unit: EEPROM write failed");
    return;
  }
  unit_number = (uint32_t)v;
  unit_format(unit, sizeof(unit));
  uart.printf("unit: set %s\r\n", unit);
}

// "51.8", "-3", "+12.5" to 0.1 ppm units; at most one decimal
static bool tenths_parse(const char *s, int32_t *out) {
  bool neg = (*s == '-');
  int32_t v = 0;
  int8_t frac = -1;                 // -1 no point yet, 0 point seen, 1 one decimal read

  if (*s == '+' || *s == '-') {
    s++;
  }
  if (*s < '0' || *s > '9') {
    return false;
  }
  for (; *s; s++) {
    if (*s == '.' && frac < 0) {
      frac = 0;
      continue;
    }
    if (*s < '0' || *s > '9' || frac > 0 || v > 100000) {
      return false;
    }
    v = v * 10 + (*s - '0');
    if (frac == 0) {
      frac = 1;
    }
  }
  if (frac < 1) {
    v *= 10;
  }
  *out = neg ? -v : v;
  return true;
}

// C<ppm>: the drift measured with no correction, + if the clock runs fast; C0 clears.
// Re-measuring with a correction in place gives the residual: send the old value plus the residual
static void rtc_cal_set(const char *s) {
  int32_t t;
  char cal[16];

  if (!tenths_parse(s, &t) || t > RTC_CAL_MAX_TENTHS || t < -RTC_CAL_MAX_TENTHS) {
    uart.printf("rtc: bad cal '%s', send C<ppm> within +-480.0, + = runs fast\r\n", s);
    return;
  }
  if (!rtc_cal_apply(t)) {
    uart.println("rtc: cal CALR write failed");
    return;
  }
  rtc_cal_tenths = t;
  rtc_cal_format(cal, sizeof(cal));
  if (!eep_ok || !eep_write_count(EEP_ADDR_RTC_CAL, (uint32_t)t)) {
    uart.printf("rtc: cal %s applied, EEPROM write failed (lost when the cell is pulled)\r\n", cal);
    return;
  }
  uart.printf("rtc: cal %s\r\n", cal);
}

// line commands on UART RX; returns true if a line arrived
//   T<YYYYMMDD-HH:MM:SS>  set the clock
//   C<ppm>                store and apply the RTC drift correction, + = runs fast, e.g. C51.8
//   U<number>             set the unit number, 0..4294967294
//   ERASE                 erase the whole EEPROM once the button is pressed to accept
static bool rx_service(void) {
  static char line[RX_LINE_MAX];
  static uint8_t n = 0;
  bool got = false;
  char ts[18];

  while (uart.available()) {
    char c = uart.read();

    if (c != '\r' && c != '\n') {
      if (n < sizeof(line) - 1) {
        line[n++] = c;
      }
      continue;
    }
    if (n == 0) {
      continue;
    }
    line[n] = '\0';
    n = 0;
    got = true;
    if (strcmp(line, "ERASE") == 0) {
      eep_erase_armed = true;
      eep_erase_ms = millis();
      uart.printf("eep: press the button within %u s to erase all 64 KB\r\n", EEP_ERASE_CONFIRM_MS / 1000);
    } else if (line[0] == 'U') {
      unit_set(line + 1);
    } else if (line[0] == 'C') {
      rtc_cal_set(line + 1);
    } else if (line[0] == 'T' && rtc_set(line + 1) && rtc_format(ts, sizeof(ts))) {
      rtc_now_s(&sm_since_s);     // so a session in progress doesn't jump by the correction
      uart.printf("rtc: set %s\r\n", ts);
    } else {
      uart.printf("bad command '%s'\r\n", line);
    }
  }
  return got;
}

// a state's stored total plus the session in progress, in whole minutes (seconds with DBG_EN);
// "?" if the EEPROM can't be read
static void sm_total_format(uint16_t addr, uint8_t state, char *buf, size_t len) {
  uint32_t s, now;

  if (!eep_ok || !eep_read_count(addr, &s)) {
    snprintf(buf, len, "?");
    return;
  }
  if (sm_state == state && rtc_now_s(&now) && now > sm_since_s) {
    s += now - sm_since_s;
  }
  snprintf(buf, len, "%lu", DBG_EN ? s : s / 60);
}

// one status line: time, unit number, temperature, supply, idle and operating totals in minutes, e.g.
// 20261105-21:50:40 u2 23.7C 2704mV id0m op0m  (temperature needs the accel running, as it is while awake)
// with DBG_EN it's tagged DBG and the totals are in seconds: 20261105-21:50:40 DBG u2 23.7C 2704mV id0s op0s
static void print_status(void) {
  char ts[18], unit[12], temp[10], idle[12], op[12];
  int32_t dc;
  uint32_t vcc = vcc_read_mv();

  if (!rtc_format(ts, sizeof(ts))) {
    strcpy(ts, "rtc unset");
  }
  if (unit_number == EEP_UNIT_UNSET) {
    strcpy(unit, "-");
  } else {
    unit_format(unit, sizeof(unit));
  }
  if (acc_ok && acc_read_temp_dc(&dc)) {
    snprintf(temp, sizeof(temp), "%s%ld.%ld", dc < 0 ? "-" : "", labs(dc) / 10, labs(dc) % 10);
  } else {
    strcpy(temp, "?");
  }
  sm_total_format(EEP_ADDR_IDLE_S, HUM_BRACKET_IDLE, idle, sizeof(idle));
  sm_total_format(EEP_ADDR_OP_S, HUM_BRACKET_OP, op, sizeof(op));
  const char *u = DBG_EN ? "s" : "m";
  uart.printf("%s %su%s %sC %lumV id%s%s op%s%s\r\n", ts, DBG_EN ? "DBG " : "", unit, temp, vcc, idle, u, op, u);
}


// timestamped "DBG ..." line when DBG_EN is on
static void dbg(const char *fmt, ...) {
  char ts[18], msg[40];
  va_list ap;

  if (!DBG_EN) {
    return;
  }
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (!rtc_format(ts, sizeof(ts))) {
    strcpy(ts, "rtc unset");
  }
  uart.printf("%s DBG %s\r\n", ts, msg);
}

// add the time spent in the state being left (idle or operating) to its EEPROM total
static void sm_store(uint8_t state, uint32_t now) {
  uint16_t addr = state == HUM_BRACKET_IDLE ? EEP_ADDR_IDLE_S : EEP_ADDR_OP_S;
  uint32_t total;

  if (!eep_ok || !eep_read_count(addr, &total) ||
      !eep_write_count(addr, total + (now > sm_since_s ? now - sm_since_s : 0))) {
    uart.println("sm: EEPROM write failed");
  }
}

// one measurement: frames until two agree, at most HUM_MAX_FRAMES; NONE if they never agree
static uint8_t sm_measure(void) {
  if (!acc_ok) {
    return HUM_BRACKET_NONE;
  }
  hum_start();
  hum_frames = 0;
  hum_measuring = true;
  uint32_t start = millis(), limit = HUM_MAX_FRAMES * HUM_N * HUM_BLOCKS * 1000UL / 800U + 1000U;
  while (hum_state == HUM_BRACKET_NONE && hum_frames < HUM_MAX_FRAMES && millis() - start < limit) {
    hum_service();
    led_service();
  }
  hum_measuring = false;
  return hum_state;
}

// measure and act on the result; an undecided measurement keeps the current state
static void sm_cycle(void) {
  uint8_t next = sm_measure();
  uint32_t now;
  char ts[18];

  dbg("sampled %s", next == HUM_BRACKET_NONE ? "undecided" : hum_names[next]);
  if (next == HUM_BRACKET_NONE || next == sm_state || !rtc_now_s(&now)) {
    return;
  }
  if (sm_state != HUM_BRACKET_OFF) {
    sm_store(sm_state, now);
  }
  rtc_format(ts, sizeof(ts));
  uart.printf("%s %s->%s\r\n", ts, hum_names[sm_state], hum_names[next]);
  sm_state = next;
  sm_since_s = now;
}

// light sleep (idle, operating): accel off, the RTC timer or the button wakes it;
// deep sleep (off): no timer, only the button or motion wakes it
static void sm_sleep(void) {
  bool deep = sm_state == HUM_BRACKET_OFF;

  dbg("sleep %s", deep ? "deep" : "light");
  hum_stop(deep && ACC_WAKE_EN);
  rtc_woke = false;
  if (!deep) {
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, SM_LIGHT_SLEEP_S - 1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
  }
  mcu_sleep();
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  // the calendar shadow registers are stale after Stop until resynchronized
  __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
  HAL_RTC_WaitForSynchro(&hrtc);
  __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
  dbg("wake %s", btn_woke ? "btn" : acc_woke ? "motion" : rtc_woke ? "timer" : "?");
}


void loop() {
  static uint32_t awake_ms = 0;   // start of the current awake window

  led_service();
  hum_service();
  if (rx_service()) {
    awake_ms = millis();
  }
  if (btn_pressed()) {
    if (eep_erase_armed) {
      eep_erase_armed = false;
      uart.println("eep: erasing...");
      uint32_t start = millis();
      if (eep_erase()) {
        uart.printf("eep: erased in %lu ms\r\n", millis() - start);
      }
      hum_reset();                // the FIFO overran during the erase; start a clean frame
    } else {
      print_status();
    }
    awake_ms = millis();
  }
  if (eep_erase_armed && millis() - eep_erase_ms >= EEP_ERASE_CONFIRM_MS) {
    eep_erase_armed = false;
    uart.println("eep: erase cancelled, no button press");
  }
  // motion while awake: just re-arm INT1, don't extend the window
  if (acc_woke) {
    acc_woke = false;
    acc_clear_int();
  }
  btn_woke = false;

  // stay awake while the clock is unset so the LED can flash
  if (!rtc_is_set() || millis() - awake_ms < AWAKE_MS) {
    return;
  }

  sm_cycle();
  digitalWrite(LED_PIN, LOW);     // in case sleep lands mid-blip
  led_blips = 0;
  sm_sleep();

  if (btn_woke) {
    // the press that woke us counts; debounce takes over from here so it isn't counted twice
    btn_down = true;
    btn_stable_ms = millis();
    digitalWrite(LED_PIN, HIGH);
    delay(LED_WAKE_MS);
    digitalWrite(LED_PIN, LOW);
    hum_start();
    delay(HUM_FIRST_MS);
    hum_service();
    print_status();
    awake_ms = millis();
  } else {
    awake_ms = millis() - AWAKE_MS;   // timer or motion: measure straight away, no awake window
  }
  btn_woke = false;
  acc_woke = false;
  rtc_woke = false;
}
