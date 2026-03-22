#include "uart_drv.hpp"
#include "board/pins.hpp"

#include "driver/uart.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

bool initUart(uart_port_t port, int tx, int rx, int baud) {
  uart_config_t cfg{};
  cfg.baud_rate = baud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

  if (!uart_is_driver_installed(port)) {
    if (uart_driver_install(port, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
  }
  if (uart_param_config(port, &cfg) != ESP_OK) return false;
  if (uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;
  return true;
}

} // namespace

bool UartDrv::initSimUart() {
  return initUart((uart_port_t)pins::UART_SIM_NUM, pins::UART_SIM_TX, pins::UART_SIM_RX, pins::UART_SIM_BAUD);
}

bool UartDrv::initSimUart(int baud) {
  return initUart((uart_port_t)pins::UART_SIM_NUM, pins::UART_SIM_TX, pins::UART_SIM_RX, baud);
}

bool UartDrv::initSensorUart() {
  return initUart((uart_port_t)pins::UART_SENSOR_NUM, pins::UART_SENSOR_TX, pins::UART_SENSOR_RX, pins::UART_SENSOR_BAUD);
}

int UartDrv::writeSim(const uint8_t* data, int len) {
  return uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, (const char*)data, len);
}

int UartDrv::writeSensor(const uint8_t* data, int len) {
  return uart_write_bytes((uart_port_t)pins::UART_SENSOR_NUM, (const char*)data, len);
}

int UartDrv::readSim(uint8_t* out, int maxLen, uint32_t timeoutMs) {
  return uart_read_bytes((uart_port_t)pins::UART_SIM_NUM, out, maxLen, pdMS_TO_TICKS(timeoutMs));
}

int UartDrv::readSensor(uint8_t* out, int maxLen, uint32_t timeoutMs) {
  return uart_read_bytes((uart_port_t)pins::UART_SENSOR_NUM, out, maxLen, pdMS_TO_TICKS(timeoutMs));
}

void UartDrv::flushSim() {
  uart_flush_input((uart_port_t)pins::UART_SIM_NUM);
}

void UartDrv::flushSensor() {
  uart_flush_input((uart_port_t)pins::UART_SENSOR_NUM);
}

bool UartDrv::writeLineSim(const char* line) {
  if (!line) return false;
  uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, line, (int)strlen(line));
  uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, "\r", 1);
  return true;
}

std::string UartDrv::readLineSim(uint32_t timeoutMs) {
  std::string s;
  uint8_t ch{};
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    int n = readSim(&ch, 1, 50);
    if (n == 1) {
      s.push_back((char)ch);
    }
  }
  return s;
}
