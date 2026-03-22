#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "board/pins.hpp"
#include "board/gpio_drv.hpp"
#include "board/adc_drv.hpp"
#include "board/i2c_drv.hpp"
#include "modules/io/io_controller.hpp"
#include "common/nvs_store.hpp"

// Test function declarations
extern void test_rtc();
extern void test_adc();
extern void test_laser();
extern void test_ultrasonic();
extern void test_sim();
extern void test_dcom();
extern void test_gpio();
extern void test_i2c_scan();
extern void test_timesync();

static const char* TAG = "TestMain";

// ──────────────────────────────────────────────
// Console line reader using getchar/putchar (no UART driver needed)
// ──────────────────────────────────────────────
static bool readLine(char* buf, int maxLen, uint32_t timeoutMs) {
  int pos = 0;
  uint32_t start = (uint32_t)xTaskGetTickCount();

  while (pos < maxLen - 1) {
    if (timeoutMs > 0 && pdTICKS_TO_MS(xTaskGetTickCount() - start) > timeoutMs) {
      break;
    }

    int ch = getchar();
    if (ch == EOF) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // Echo
    putchar(ch);
    fflush(stdout);

    if (ch == '\r' || ch == '\n') {
      putchar('\n');
      fflush(stdout);
      break;
    }

    // Backspace
    if (ch == 0x08 || ch == 0x7F) {
      if (pos > 0) {
        pos--;
        printf("\b \b");
        fflush(stdout);
      }
      continue;
    }

    buf[pos++] = (char)ch;
  }

  buf[pos] = '\0';

  // Trim trailing spaces
  while (pos > 0 && buf[pos - 1] == ' ') {
    buf[--pos] = '\0';
  }

  // Trim leading spaces
  char* start_ptr = buf;
  while (*start_ptr == ' ') start_ptr++;
  if (start_ptr != buf) {
    memmove(buf, start_ptr, strlen(start_ptr) + 1);
  }

  return strlen(buf) > 0;
}

// ──────────────────────────────────────────────
// Menu
// ──────────────────────────────────────────────
static void printMenu() {
  printf("\n");
  printf("========== COMPONENT TEST MENU ==========\n");
  printf("  help            - Show this menu\n");
  printf("  test rtc        - Test RTC PCF8563\n");
  printf("  test adc        - Test ADC voltage\n");
  printf("  test laser      - Test Laser sensor\n");
  printf("  test ultrasonic - Test Ultrasonic sensor\n");
  printf("  test sim        - Test SIM 4G module\n");
  printf("  test dcom       - Test DCOM Wi-Fi\n");
  printf("  test gpio       - Test GPIO outputs\n");
  printf("  test i2c        - Test I2C bus scan\n");
  printf("  test timesync   - Sync RTC from server time\n");
  printf("  test all        - Run all tests\n");
  printf("  reboot          - Restart ESP32\n");
  printf("==========================================\n");
}

// ──────────────────────────────────────────────
// Command dispatcher
// ──────────────────────────────────────────────
static void dispatchCommand(const char* cmd) {
  if (strcmp(cmd, "help") == 0) {
    printMenu();
  } else if (strcmp(cmd, "test rtc") == 0) {
    test_rtc();
  } else if (strcmp(cmd, "test adc") == 0) {
    test_adc();
  } else if (strcmp(cmd, "test laser") == 0) {
    test_laser();
  } else if (strcmp(cmd, "test ultrasonic") == 0) {
    test_ultrasonic();
  } else if (strcmp(cmd, "test sim") == 0) {
    test_sim();
  } else if (strcmp(cmd, "test dcom") == 0) {
    test_dcom();
  } else if (strcmp(cmd, "test gpio") == 0) {
    test_gpio();
  } else if (strcmp(cmd, "test i2c") == 0) {
    test_i2c_scan();
  } else if (strcmp(cmd, "test timesync") == 0) {
    test_timesync();
  } else if (strcmp(cmd, "test all") == 0) {
    printf("\n===== RUNNING ALL TESTS =====\n");
    test_rtc();
    test_adc();
    test_i2c_scan();
    test_gpio();
    test_laser();
    test_ultrasonic();
    test_sim();
    test_dcom();
    test_timesync();
    printf("===== ALL TESTS COMPLETE =====\n\n");
  } else if (strcmp(cmd, "reboot") == 0) {
    printf("Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
  } else {
    printf("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
  }
}

// ──────────────────────────────────────────────
// Entry point
// ──────────────────────────────────────────────
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "=== COMPONENT TEST MODE ===");

  // UART0 is already configured by ESP-IDF console — no driver install needed.
  // readLine() uses getchar()/putchar() via VFS.

  // Init basic peripherals (all power OFF by default)
  NvsStore::init();
  IoController::instance().init();
  AdcDrv::init();

  ESP_LOGI(TAG, "All peripherals initialized (power OFF)");

  // Show menu
  printMenu();

  // Main command loop (no FreeRTOS tasks - sequential only)
  char cmdBuf[64];
  while (true) {
    printf("> ");
    fflush(stdout);

    if (readLine(cmdBuf, sizeof(cmdBuf), 0)) {
      dispatchCommand(cmdBuf);
    }
  }
}
