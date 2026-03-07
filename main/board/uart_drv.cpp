#include "uart_drv.hpp"
#include "board/pins.hpp"

#include "driver/uart.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool UartDrv::initSimUart() {
  uart_config_t cfg{};
  cfg.baud_rate = pins::UART_SIM_BAUD;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

  if (uart_param_config((uart_port_t)pins::UART_SIM_NUM, &cfg) != ESP_OK) return false;
  if (uart_set_pin((uart_port_t)pins::UART_SIM_NUM, pins::UART_SIM_TX, pins::UART_SIM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;
  if (uart_driver_install((uart_port_t)pins::UART_SIM_NUM, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
  return true;
}

int UartDrv::writeSim(const uint8_t* data, int len) {
  return uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, (const char*)data, len);
}

int UartDrv::readSim(uint8_t* out, int maxLen, uint32_t timeoutMs) {
  return uart_read_bytes((uart_port_t)pins::UART_SIM_NUM, out, maxLen, pdMS_TO_TICKS(timeoutMs));
}

bool UartDrv::writeLineSim(const char* line) {
  if (!line) return false;
  uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, line, (int)strlen(line));
  uart_write_bytes((uart_port_t)pins::UART_SIM_NUM, "\r\n", 2);
  return true;
}

std::string UartDrv::readLineSim(uint32_t timeoutMs) {
  std::string s;
  uint8_t ch{};
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    int n = readSim(&ch, 1, 50);
    if (n == 1) {
      if (ch == '\n') break;
      if (ch != '\r') s.push_back((char)ch);
    }
  }
  return s;
}
