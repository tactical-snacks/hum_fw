#include <Arduino.h>
#include <Wire.h>


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
#define LED_UNSET_ON_MS      5U      // clock-unset blip: 5 ms on every LED_UNSET_PERIOD_MS
#define LED_UNSET_PERIOD_MS  1000U
#define LED_WAKE_MS          50U     // flash on each wake
#define AWAKE_MS             5000U   // stay up after a wake for button presses and serial commands
#define RX_LINE_MAX          32U

// LIS2DTW12 registers (datasheet DS12825 section 8)
#define ACC_REG_OUT_T_L      0x0DU   // temperature, 12-bit LE left-justified, 16 LSB/C, 0 = 25 C
#define ACC_REG_WHO_AM_I     0x0FU   // reads 0x44
#define ACC_REG_CTRL1        0x20U   // ODR[7:4], MODE[3:2], LP_MODE[1:0]
#define ACC_REG_CTRL2        0x21U   // BOOT, SOFT_RESET, -, CS_PU_DISC, BDU, IF_ADD_INC, I2C_DISABLE, SIM
#define ACC_REG_CTRL3        0x22U   // ST[7:6], PP_OD, LIR, H_LACTIVE, -, SLP_MODE_SEL, SLP_MODE_1
#define ACC_REG_CTRL4        0x23U   // INT1 pad routing, bit 5 INT1_WU
#define ACC_REG_CTRL6        0x25U   // BW_FILT[7:6], FS[5:4], FDS, LOW_NOISE
#define ACC_REG_OUT_X_L      0x28U   // X, Y, Z as 16-bit LE, left-justified
#define ACC_REG_WAKE_UP_THS  0x34U   // SINGLE_DOUBLE_TAP, SLEEP_ON, WK_THS[5:0]
#define ACC_REG_ALL_INT_SRC  0x3BU   // reading clears latched interrupts
#define ACC_REG_CTRL7        0x3FU   // DRDY_PULSED, INT2_ON_INT1, INTERRUPTS_ENABLE, ...

#define ACC_WHO_AM_I_VAL     0x44U
#define ACC_CTRL1_12HZ5_LP1  0x20U   // ODR 12.5 Hz, low-power mode 1 (12-bit, ~1 uA)
#define ACC_CTRL2_SOFT_RESET 0x40U
#define ACC_CTRL2_BDU_INC    0x0CU   // block data update, address auto-increment
#define ACC_CTRL3_LIR        0x10U   // latch INT1 until ALL_INT_SRC is read
#define ACC_CTRL4_INT1_WU    0x20U
#define ACC_CTRL6_2G         0x00U   // +/-2 g, ODR/2 bandwidth, low-noise off
#define ACC_CTRL7_INT_EN     0x20U   // INTERRUPTS_ENABLE
#define ACC_WU_THS           2U      // wake on motion over 2 x 31.25 mg (1/64 of FS), high-pass filtered
#define ACC_INT_DROP_MS      200U    // INT1 drops on the next internal sample after a clear (80 ms at 12.5 Hz)
#define ACC_UG_PER_LSB       244     // +/-2 g: 0.244 mg per 14-bit LSB

// 24FC512: 64 KB, 2-byte address, 128-byte pages, 5 ms max write cycle, WP = GND
// no sleep command: it drops to standby (1 uA max) whenever the bus is idle and no write is running
#define EEP_PAGE_SIZE        128U
#define EEP_WRITE_TIMEOUT_MS 10U
#define EEP_ADDR_BOOT_COUNT  0x0000U  // uint32_t LE, 0xFFFFFFFF when erased


// the variant's default Serial is USART4 on PA0/PA1 (ACC_INT1/BTN), so use USART1
Uart uart(UART_RX_PIN, UART_TX_PIN);

static RTC_HandleTypeDef hrtc;

static bool lse_ok = false;
static bool rtc_ok = false;
static bool acc_ok = false;
static bool eep_ok = false;
static volatile bool btn_woke = false;      // set by the PA1 / PA0 EXTI handlers
static volatile bool acc_woke = false;
static bool btn_down = false;               // debounced button state
static uint32_t btn_stable_ms = 0;


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

// returns acceleration in mg; low-power mode 1 zeroes the bottom 2 bits of the 14-bit field,
// so the 14-bit scale still applies at 12-bit (0.976 mg) resolution
static bool acc_read_mg(int32_t *x, int32_t *y, int32_t *z) {
  uint8_t buf[6];

  if (!acc_read(ACC_REG_OUT_X_L, buf, sizeof(buf))) {
    return false;
  }
  *x = ((int16_t)(buf[1] << 8 | buf[0]) >> 2) * ACC_UG_PER_LSB / 1000;
  *y = ((int16_t)(buf[3] << 8 | buf[2]) >> 2) * ACC_UG_PER_LSB / 1000;
  *z = ((int16_t)(buf[5] << 8 | buf[4]) >> 2) * ACC_UG_PER_LSB / 1000;
  return true;
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

// probe the device and bump the boot counter
static bool eep_init(void) {
  uint32_t boots;

  if (!eep_wait_ready()) {
    uart.println("eep: no ACK at 0x50");
    return false;
  }
  if (!eep_read_count(EEP_ADDR_BOOT_COUNT, &boots) ||
      !eep_write_count(EEP_ADDR_BOOT_COUNT, ++boots)) {
    uart.println("eep: boot count read/write failed");
    return false;
  }
  uart.printf("eep: boot count %lu\r\n", boots);
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


static void btn_isr(void) {
  btn_woke = true;
}

static void acc_isr(void) {
  acc_woke = true;
}

// Stop 2: RTC, LSE and the EXTI wake pins keep running, SRAM is kept, everything else stops.
// SWD drops out too, so uploads connect under reset (see upload_flags in platformio.ini).
static void mcu_sleep(void) {
  acc_clear_int();
  // wait for INT1 to actually drop; if it doesn't, motion is ongoing and the loop below wakes at once
  uint32_t start = millis();
  while (digitalRead(ACC_INT1_PIN) == HIGH && millis() - start < ACC_INT_DROP_MS) {
  }
  uart.flush();

  // stop SysTick before masking, or a tick left pending ends WFI at once
  HAL_SuspendTick();
  // with interrupts masked, an edge that lands after a check still ends WFI immediately;
  // unmasking briefly after each wake lets the EXTI handler set its flag
  __disable_irq();
  while (!btn_woke && !acc_woke) {
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

  uart.println("hello");
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

  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btn_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(ACC_INT1_PIN), acc_isr, RISING);
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

// clock unset: 5 ms blip each second (swap for Flasher's portable branch once it's public)
static void led_service(void) {
  static bool blip = false;
  bool on = !rtc_is_set() && millis() % LED_UNSET_PERIOD_MS < LED_UNSET_ON_MS;

  if (on != blip) {
    blip = on;
    digitalWrite(LED_PIN, on ? HIGH : LOW);
  }
}

// line commands on UART RX: T<YYYYMMDD-HH:MM:SS> sets the clock; returns true if a line arrived
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
    if (line[0] == 'T' && rtc_set(line + 1) && rtc_format(ts, sizeof(ts))) {
      uart.printf("rtc: set %s\r\n", ts);
    } else {
      uart.printf("rtc: bad command '%s'\r\n", line);
    }
  }
  return got;
}

// one status line: time, what triggered it, acceleration, temperature, supply
static void print_status(const char *why) {
  char ts[18];
  int32_t x, y, z, dc;
  uint32_t vcc = vcc_read_mv();

  if (!rtc_format(ts, sizeof(ts))) {
    strcpy(ts, "rtc unset");
  }
  if (acc_ok && acc_read_mg(&x, &y, &z) && acc_read_temp_dc(&dc)) {
    uart.printf("%s  %s  x=%5ld y=%5ld z=%5ld mg  temp %s%ld.%ld C  vcc %lu mV\r\n", ts, why,
                x, y, z, dc < 0 ? "-" : "", labs(dc) / 10, labs(dc) % 10, vcc);
  } else {
    uart.printf("%s  %s  acc: read failed  vcc %lu mV\r\n", ts, why, vcc);
  }
}


void loop() {
  static uint32_t awake_ms = 0;   // start of the current awake window

  led_service();
  if (rx_service()) {
    awake_ms = millis();
  }
  if (btn_pressed()) {
    print_status("wake btn");
    awake_ms = millis();
  }
  // motion while awake: just re-arm INT1, don't extend the window
  if (acc_woke) {
    acc_woke = false;
    acc_clear_int();
  }
  btn_woke = false;

  // stay awake while the clock is unset so the LED can blip
  if (!rtc_is_set() || millis() - awake_ms < AWAKE_MS) {
    return;
  }

  mcu_sleep();

  const char *why = btn_woke ? "wake btn" : "wake acc";
  if (btn_woke) {
    // the press that woke us counts; debounce takes over from here so it isn't counted twice
    btn_down = true;
    btn_stable_ms = millis();
  }
  btn_woke = false;
  acc_woke = false;
  digitalWrite(LED_PIN, HIGH);
  delay(LED_WAKE_MS);
  digitalWrite(LED_PIN, LOW);
  print_status(why);
  awake_ms = millis();
}
