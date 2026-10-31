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
#define ACC_REG_CTRL3        0x22U   // ST[7:6], PP_OD, LIR, H_LACTIVE, -, SLP_MODE_SEL, SLP_MODE_1
#define ACC_REG_CTRL4        0x23U   // INT1 pad routing, bit 0 INT1_DRDY
#define ACC_REG_CTRL5        0x24U   // INT2 pad routing, bit 0 INT2_DRDY
#define ACC_REG_CTRL6        0x25U   // BW_FILT[7:6], FS[5:4], FDS, LOW_NOISE
#define ACC_REG_OUT_X_L      0x28U   // X, Y, Z as 16-bit LE, 14-bit left-justified
#define ACC_REG_CTRL7        0x3FU   // DRDY_PULSED, INT2_ON_INT1, INTERRUPTS_ENABLE, ...

#define ACC_WHO_AM_I_VAL     0x44U
#define ACC_CTRL1_12HZ5_HP   0x24U   // ODR 12.5 Hz, high-performance (14-bit)
#define ACC_CTRL2_SOFT_RESET 0x40U
#define ACC_CTRL2_BDU_INC    0x0CU   // block data update, address auto-increment
#define ACC_CTRL3_H_LACTIVE  0x08U   // interrupt pads active low (default push-pull, active high)
#define ACC_CTRL4_INT1_DRDY  0x01U
#define ACC_CTRL5_INT2_DRDY  0x01U
#define ACC_CTRL6_2G         0x00U   // +/-2 g, ODR/2 bandwidth, low-noise off
#define ACC_CTRL7_DRDY_PULSE 0xA0U   // DRDY_PULSED, INTERRUPTS_ENABLE
#define ACC_UG_PER_LSB       244     // +/-2 g, 14-bit: 0.244 mg/LSB


// the variant's default Serial is USART4 on PA0/PA1 (ACC_INT1/BTN), so use USART1
Uart uart(UART_RX_PIN, UART_TX_PIN);

static bool lse_ok = false;
static bool acc_ok = false;
static volatile uint32_t acc_int1_count = 0;
static volatile uint32_t acc_int2_count = 0;


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

// with nothing routed the pads sit inactive, so flipping H_LACTIVE should flip PA0/PA2
// pull-ups on during the test: 0/1 = ok, 1/1 = open joint, 0/0 = shorted low
static bool acc_int_pin_test(void) {
  int lo1, lo2, hi1, hi2;

  pinMode(ACC_INT1_PIN, INPUT_PULLUP);
  pinMode(ACC_INT2_PIN, INPUT_PULLUP);
  acc_write(ACC_REG_CTRL3, 0);
  delay(1);
  lo1 = digitalRead(ACC_INT1_PIN);
  lo2 = digitalRead(ACC_INT2_PIN);
  acc_write(ACC_REG_CTRL3, ACC_CTRL3_H_LACTIVE);
  delay(1);
  hi1 = digitalRead(ACC_INT1_PIN);
  hi2 = digitalRead(ACC_INT2_PIN);
  acc_write(ACC_REG_CTRL3, 0);
  pinMode(ACC_INT1_PIN, INPUT);
  pinMode(ACC_INT2_PIN, INPUT);

  bool ok1 = lo1 == LOW && hi1 == HIGH;
  bool ok2 = lo2 == LOW && hi2 == HIGH;
  uart.printf("acc: INT1 (PA0) idle %d/%d %s\r\n", lo1, hi1, ok1 ? "ok" : "FAIL");
  uart.printf("acc: INT2 (PA2) idle %d/%d %s\r\n", lo2, hi2, ok2 ? "ok" : "FAIL");
  return ok1 && ok2;
}

static void acc_int1_isr(void) {
  acc_int1_count++;
}

static void acc_int2_isr(void) {
  acc_int2_count++;
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

  acc_int_pin_test();

  // DRDY pulsed on both pads so each pin should see ODR edges/s
  return acc_write(ACC_REG_CTRL2, ACC_CTRL2_BDU_INC) &&
         acc_write(ACC_REG_CTRL6, ACC_CTRL6_2G) &&
         acc_write(ACC_REG_CTRL4, ACC_CTRL4_INT1_DRDY) &&
         acc_write(ACC_REG_CTRL5, ACC_CTRL5_INT2_DRDY) &&
         acc_write(ACC_REG_CTRL7, ACC_CTRL7_DRDY_PULSE) &&
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


void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ACC_INT1_PIN, INPUT);
  pinMode(ACC_INT2_PIN, INPUT);
  uart.begin(UART_BAUD);

  // the variant's default Wire is PA6/PA7, so move it to I2C1 on PB6/PB7
  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();

  uart.println("hello");
  uart.println(lse_ok ? "lse: ok" : "lse: FAIL, MSI untrimmed");
  acc_ok = acc_init();
  uart.println(acc_ok ? "acc: ok" : "acc: init failed");

  attachInterrupt(digitalPinToInterrupt(ACC_INT1_PIN), acc_int1_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(ACC_INT2_PIN), acc_int2_isr, RISING);
}

void loop() {
  static uint32_t last_ms = 0, last_int1 = 0, last_int2 = 0;
  int32_t x, y, z;

  digitalWrite(LED_PIN, HIGH);
  if (acc_ok && acc_read_mg(&x, &y, &z)) {
    uint32_t now = millis();
    uint32_t n1 = acc_int1_count;
    uint32_t n2 = acc_int2_count;
    uint32_t dt = now - last_ms;

    // expect ~12 Hz on both at ODR 12.5 Hz
    uart.printf("x=%5ld y=%5ld z=%5ld mg  int1=%2lu int2=%2lu Hz\r\n", x, y, z,
                (n1 - last_int1) * 1000 / dt, (n2 - last_int2) * 1000 / dt);
    last_ms = now;
    last_int1 = n1;
    last_int2 = n2;
  } else {
    uart.println("acc: read failed");
  }
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}