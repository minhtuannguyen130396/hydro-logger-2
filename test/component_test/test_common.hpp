#pragma once

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Print helpers for test results
#define TEST_START(name) \
  printf("\n[TEST-%s] === START ===\n", name)

#define TEST_PASS(name) \
  printf("[TEST-%s] === PASS ===\n\n", name)

#define TEST_FAIL(name, reason) \
  printf("[TEST-%s] === FAIL === (%s)\n\n", name, reason)

#define TEST_INFO(name, fmt, ...) \
  printf("[TEST-%s] " fmt "\n", name, ##__VA_ARGS__)

// Delay helper
inline void testDelayMs(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}
