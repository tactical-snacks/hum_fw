#include <Arduino.h>



#define LED_GPIO_Port        GPIOA   // PA8,  pin 18, active high
#define LED_Pin              GPIO_PIN_8
#define BTN_GPIO_Port        GPIOA   // PA1,  pin 7,  active low, pull-up, WKUP3
#define BTN_Pin              GPIO_PIN_1
#define ACC_INT1_GPIO_Port   GPIOA   // PA0,  pin 6,  WKUP1
#define ACC_INT1_Pin         GPIO_PIN_0
#define ACC_INT2_GPIO_Port   GPIOA   // PA2,  pin 8,  WKUP4
#define ACC_INT2_Pin         GPIO_PIN_2
#define I2C_SCL_GPIO_Port    GPIOB   // PB6,  pin 29, AF4 I2C1
#define I2C_SCL_Pin          GPIO_PIN_6
#define I2C_SDA_GPIO_Port    GPIOB   // PB7,  pin 30, AF4 I2C1
#define I2C_SDA_Pin          GPIO_PIN_7
#define UART_TX_GPIO_Port    GPIOA   // PA9,  pin 19, AF7 USART1
#define UART_TX_Pin          GPIO_PIN_9
#define UART_RX_GPIO_Port    GPIOA   // PA10, pin 20, AF7 USART1
#define UART_RX_Pin          GPIO_PIN_10
#define EEPROM_I2C_ADDR      0x50U   // 24FC512, A2..A0 = GND
#define ACC_I2C_ADDR         0x19U   // LIS2DTW12, SA0 = VCC


void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
}