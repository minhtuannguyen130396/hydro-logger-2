#pragma once
#include "driver/gpio.h"

namespace pins {

// ==============================================================================
// 1. GIAO TIEP I2C (RTC PCF8563)
// ==============================================================================
static constexpr int I2C_NUM               = 0;
static constexpr gpio_num_t I2C_SDA        = GPIO_NUM_21; // Data SDA
static constexpr gpio_num_t I2C_SCL        = GPIO_NUM_22; // Clock SCL
static constexpr int I2C_FREQ_HZ           = 100000;      // 100 kHz
static constexpr uint8_t PCF8563_ADDR      = 0x51;

// ==============================================================================
// 2. CHAN WAKEUP & NUT BAM (WAKEUP & BUTTON)
// ==============================================================================
// Wakeup Pin (RTC / Ext): GPIO 36 (SENSOR_VP - Input Only, Ext0 wakeup active-low)
static constexpr gpio_num_t WAKEUP         = GPIO_NUM_36;
// Button Pin: GPIO 33 (Nut bam chuyen che do Config/Measuring)
static constexpr gpio_num_t BUTTON         = GPIO_NUM_33;

// ==============================================================================
// 3. GIAO TIEP UART VOI CAM BIEN LASER (LASER DISTANCE SENSOR - UART 1)
// ==============================================================================
static constexpr int UART_SENSOR_NUM       = 1;
static constexpr int UART_LASER_TX         = GPIO_NUM_18; // TX ESP32 -> RX Cam bien Laser
static constexpr int UART_LASER_RX         = GPIO_NUM_19; // RX ESP32 <- TX Cam bien Laser
static constexpr int UART_SENSOR_LASER_BAUD = 19200;

static constexpr int UART_SUPERSONIC_TX    = GPIO_NUM_18;
static constexpr int UART_SUPERSONIC_RX    = GPIO_NUM_19;
static constexpr int UART_SENSOR_SUPERSONIC_BAUD = 9600;

#define PINS_UART1_DEVICE_LASER 1
#define PINS_UART1_DEVICE_SUPERSONIC 2
#ifndef PINS_UART1_DEVICE
#define PINS_UART1_DEVICE PINS_UART1_DEVICE_LASER
#endif

#define SENSOR_DEVICE_PRESSURE 1
#define SENSOR_DEVICE_UART1    2
#ifndef SENSOR_DEVICE
#define SENSOR_DEVICE SENSOR_DEVICE_UART1
#endif

// Power control pins cho cam bien
// DCOM va Laser dung chung mot chan nguon GPIO 26 (keo xuong 0 khi ON, 1 khi OFF)
static constexpr gpio_num_t DCOM_PWR       = GPIO_NUM_26; // Bat/Tat nguon DCOM (4G) & Laser, pull-up enabled
static constexpr gpio_num_t LASER_PWR      = DCOM_PWR;    // Laser chung chan nguon voi DCOM (GPIO 26)
static constexpr gpio_num_t ULTRA_PWR      = GPIO_NUM_NC;
static constexpr gpio_num_t SIM_PWR        = GPIO_NUM_NC;

#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
static constexpr int UART_SENSOR_TX        = UART_LASER_TX;
static constexpr int UART_SENSOR_RX        = UART_LASER_RX;
static constexpr int UART_SENSOR_BAUD      = UART_SENSOR_LASER_BAUD;
static constexpr gpio_num_t UART_SENSOR_PWR = LASER_PWR;
#elif PINS_UART1_DEVICE == PINS_UART1_DEVICE_SUPERSONIC
static constexpr int UART_SENSOR_TX        = UART_SUPERSONIC_TX;
static constexpr int UART_SENSOR_RX        = UART_SUPERSONIC_RX;
static constexpr int UART_SENSOR_BAUD      = UART_SENSOR_SUPERSONIC_BAUD;
static constexpr gpio_num_t UART_SENSOR_PWR = ULTRA_PWR;
#else
#error "PINS_UART1_DEVICE must be PINS_UART1_DEVICE_LASER or PINS_UART1_DEVICE_SUPERSONIC"
#endif

// ==============================================================================
// 4. CHAN DIEU KHIEN ON / OFF DCOM VA LASER (NGUON CHUNG GPIO 26)
// ==============================================================================
// DCOM_PWR va LASER_PWR da duoc dinh nghia o tren (GPIO_NUM_26)

// ==============================================================================
// 5. DEN LED TIN HIEU (STATUS LED)
// ==============================================================================
static constexpr gpio_num_t LED            = GPIO_NUM_25; // Den bao trang thai

// ==============================================================================
// 6. DO DIEN AP NGUON / PIN (ADC BATTERY VOLTAGE)
// ==============================================================================
static constexpr gpio_num_t ESP_VOLTAGE_ADC_PIN = GPIO_NUM_34; // GPIO 34
static constexpr int ADC_CHAN              = 6;                // ADC1_CHANNEL_6 = GPIO34
static constexpr gpio_num_t R_SIGNAL       = GPIO_NUM_NC;
static constexpr gpio_num_t L_SIGNAL       = GPIO_NUM_NC;
static constexpr int L_SIGNAL_ADC_CHAN     = -1;

// ==============================================================================
// 7. UART PHU (UART 2 - MO RONG / DEBUG / SIM)
// ==============================================================================
static constexpr int UART_SIM_NUM          = 2;
static constexpr int UART_SIM_TX           = GPIO_NUM_4;  // TX ESP32
static constexpr int UART_SIM_RX           = GPIO_NUM_5;  // RX ESP32
static constexpr int UART_SIM_BAUD         = 115200;

static constexpr int UART_EXT_NUM          = 2;
static constexpr int UART_EXT_TX           = GPIO_NUM_4;
static constexpr int UART_EXT_RX           = GPIO_NUM_5;
static constexpr int UART_EXT_BAUD         = 115200;

// Reserved / unpopulated pins
static constexpr gpio_num_t SPEAKER        = GPIO_NUM_NC;
static constexpr gpio_num_t IO2_SYSTEM     = GPIO_NUM_NC;
static constexpr gpio_num_t IO15_SYSTEM    = GPIO_NUM_NC;

} // namespace pins
