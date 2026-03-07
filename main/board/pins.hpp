#pragma once
#include "driver/gpio.h"

namespace pins {
// Power control (adjust to your board)
static constexpr gpio_num_t LASER_PWR      = GPIO_NUM_2;
static constexpr gpio_num_t ULTRA_PWR      = GPIO_NUM_4;
static constexpr gpio_num_t SIM_PWR        = GPIO_NUM_5;
static constexpr gpio_num_t DCOM_PWR       = GPIO_NUM_18;

// Notify
static constexpr gpio_num_t LED            = GPIO_NUM_19;
static constexpr gpio_num_t SPEAKER        = GPIO_NUM_21;

// UART for SIM4G AT (adjust)
static constexpr int UART_SIM_NUM          = 1;
static constexpr int UART_SIM_TX           = GPIO_NUM_17;
static constexpr int UART_SIM_RX           = GPIO_NUM_16;
static constexpr int UART_SIM_BAUD         = 115200;

// I2C for DS1307 (adjust)
static constexpr int I2C_NUM               = 0;
static constexpr gpio_num_t I2C_SCL        = GPIO_NUM_22;
static constexpr gpio_num_t I2C_SDA        = GPIO_NUM_23;
static constexpr int I2C_FREQ_HZ           = 100000;
static constexpr uint8_t DS1307_ADDR       = 0x68;

// ADC voltage (example)
static constexpr int ADC_CHAN              = 6; // ADC1_CH6 GPIO34 on some chips
} // namespace pins
