#include <Arduino.h>


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


// the variant's default Serial is USART4 on PA0/PA1 (ACC_INT1/BTN), so use USART1
Uart uart(UART_RX_PIN, UART_TX_PIN);


void setup() {
  pinMode(LED_PIN, OUTPUT);
  uart.begin(UART_BAUD);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  uart.println("hello");
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}