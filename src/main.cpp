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

// LIS2DTW12 registers (datasheet DS12825 section 8)
#define ACC_REG_WHO_AM_I     0x0FU   // reads 0x44
#define ACC_REG_CTRL1        0x20U   // ODR[7:4], MODE[3:2], LP_MODE[1:0]
#define ACC_REG_CTRL2        0x21U   // BOOT, SOFT_RESET, -, CS_PU_DISC, BDU, IF_ADD_INC, I2C_DISABLE, SIM
#define ACC_REG_CTRL6        0x25U   // BW_FILT[7:6], FS[5:4], FDS, LOW_NOISE
#define ACC_REG_OUT_X_L      0x28U   // X, Y, Z as 16-bit LE, 14-bit left-justified

#define ACC_WHO_AM_I_VAL     0x44U
#define ACC_CTRL1_12HZ5_HP   0x24U   // ODR 12.5 Hz, high-performance (14-bit)
#define ACC_CTRL2_SOFT_RESET 0x40U
#define ACC_CTRL2_BDU_INC    0x0CU   // block data update, address auto-increment
#define ACC_CTRL6_2G         0x00U   // +/-2 g, ODR/2 bandwidth, low-noise off
#define ACC_UG_PER_LSB       244     // +/-2 g, 14-bit: 0.244 mg/LSB

// 24FC512: 64 KB, 2-byte address, 128-byte pages, 5 ms max write cycle, WP = GND
#define EEP_PAGE_SIZE        128U
#define EEP_WRITE_TIMEOUT_MS 10U
#define EEP_ADDR_BOOT_COUNT  0x0000U  // uint32_t LE, 0xFFFFFFFF when erased


// the variant's default Serial is USART4 on PA0/PA1 (ACC_INT1/BTN), so use USART1
Uart uart(UART_RX_PIN, UART_TX_PIN);

static bool lse_ok = false;
static bool acc_ok = false;


// replaces the variant's empty weak default; runs before setup()
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
         acc_write(ACC_REG_CTRL6, ACC_CTRL6_2G) &&
         acc_write(ACC_REG_CTRL1, ACC_CTRL1_12HZ5_HP);
}

// returns acceleration in mg
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

// probe the device and bump the boot counter
static bool eep_init(void) {
  uint32_t count;

  if (!eep_wait_ready()) {
    uart.println("eep: no ACK at 0x50");
    return false;
  }

  if (!eep_read(EEP_ADDR_BOOT_COUNT, (uint8_t *)&count, sizeof(count))) {
    uart.println("eep: read failed");
    return false;
  }
  count = (count == 0xFFFFFFFFUL) ? 1 : count + 1;
  uint32_t check = 0;
  if (!eep_write(EEP_ADDR_BOOT_COUNT, (const uint8_t *)&count, sizeof(count)) ||
      !eep_read(EEP_ADDR_BOOT_COUNT, (uint8_t *)&check, sizeof(check)) || check != count) {
    uart.println("eep: boot count write/verify failed");
    return false;
  }
  uart.printf("eep: boot count %lu\r\n", count);
  return true;
}


void setup() {
  pinMode(LED_PIN, OUTPUT);
  uart.begin(UART_BAUD);

  // the variant's default Wire is PA6/PA7, so move it to I2C1 on PB6/PB7
  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();

  uart.println("hello");
  uart.println(lse_ok ? "lse: ok" : "lse: FAIL, MSI untrimmed");
  acc_ok = acc_init();
  uart.println(acc_ok ? "acc: ok" : "acc: init failed");
  uart.println(eep_init() ? "eep: ok" : "eep: FAIL");
}

void loop() {
  int32_t x, y, z;

  digitalWrite(LED_PIN, HIGH);
  if (acc_ok && acc_read_mg(&x, &y, &z)) {
    uart.printf("x=%5ld y=%5ld z=%5ld mg\r\n", x, y, z);
  } else {
    uart.println("acc: read failed");
  }
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}