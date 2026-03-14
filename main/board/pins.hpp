#pragma once
#include "driver/gpio.h"

namespace pins {
// Power / control outputs from schematic
static constexpr gpio_num_t LASER_PWR      = GPIO_NUM_26;
static constexpr gpio_num_t ULTRA_PWR      = GPIO_NUM_32;
static constexpr gpio_num_t SIM_PWR        = GPIO_NUM_27;
static constexpr gpio_num_t DCOM_PWR       = GPIO_NUM_23;
static constexpr gpio_num_t LED            = GPIO_NUM_25;
static constexpr gpio_num_t WAKEUP         = GPIO_NUM_33;

// Reserved / system pins
static constexpr gpio_num_t IO2_SYSTEM     = GPIO_NUM_2;
static constexpr gpio_num_t IO15_SYSTEM    = GPIO_NUM_15;

// ADC inputs from schematic
static constexpr gpio_num_t ESP_VOLTAGE_ADC_PIN = GPIO_NUM_39; // SENSOR_VN
static constexpr int ADC_CHAN              = 3;                // ADC1_CH3 = GPIO39
static constexpr gpio_num_t R_SIGNAL       = GPIO_NUM_34;
static constexpr gpio_num_t L_SIGNAL       = GPIO_NUM_35;

// Not populated in the current schematic; keep invalid to avoid conflicting with I2C SDA on GPIO21.
static constexpr gpio_num_t SPEAKER        = GPIO_NUM_NC;

// UART2 for SIM module
static constexpr int UART_SIM_NUM          = 2;
static constexpr int UART_SIM_TX           = GPIO_NUM_5;
static constexpr int UART_SIM_RX           = GPIO_NUM_18;
static constexpr int UART_SIM_BAUD         = 115200;

// UART1 shared by laser and supersonic sensor.
// Override at build time with -DPINS_UART1_DEVICE=PINS_UART1_DEVICE_SUPERSONIC if needed.
#define PINS_UART1_DEVICE_LASER 1
#define PINS_UART1_DEVICE_SUPERSONIC 2
#ifndef PINS_UART1_DEVICE
#define PINS_UART1_DEVICE PINS_UART1_DEVICE_LASER
#endif

static constexpr int UART_SENSOR_NUM       = 1;
static constexpr int UART_SENSOR_TX        = GPIO_NUM_17;
static constexpr int UART_SENSOR_RX        = GPIO_NUM_16;
static constexpr int UART_SENSOR_BAUD      = 115200;

#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
static constexpr gpio_num_t UART_SENSOR_PWR = LASER_PWR;
#elif PINS_UART1_DEVICE == PINS_UART1_DEVICE_SUPERSONIC
static constexpr gpio_num_t UART_SENSOR_PWR = ULTRA_PWR;
#else
#error "PINS_UART1_DEVICE must be PINS_UART1_DEVICE_LASER or PINS_UART1_DEVICE_SUPERSONIC"
#endif

// I2C for RTC
static constexpr int I2C_NUM               = 0;
static constexpr gpio_num_t I2C_SCL        = GPIO_NUM_22;
static constexpr gpio_num_t I2C_SDA        = GPIO_NUM_21;
static constexpr int I2C_FREQ_HZ           = 100000;
static constexpr uint8_t DS1307_ADDR       = 0x68;
} // namespace pins
