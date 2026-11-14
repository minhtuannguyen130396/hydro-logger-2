# Project Guidelines & Autonomous Workflows for Hydro-Logger-2

## Custom Workflow: "tắt hết command uart" (Disable UART / Release Mode)
Khi người dùng nhập câu lệnh hoặc yêu cầu **"tắt hết command uart"** (hoặc tắt UART / chuyển sang chế độ release im lặng):
Thực hiện tự động quy trình sau:
1. **Khôi phục file `sdkconfig.release`:**
   - Nếu có file `sdkconfig.release.disabled`, đổi tên lại thành `sdkconfig.release`.
2. **Cập nhật `sdkconfig.defaults`:**
   - Xóa bỏ hoặc comment các dòng liên quan đến UART Console:
     ```ini
     # CONFIG_ESP_CONSOLE_UART_DEFAULT=y
     # CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
     # CONFIG_LOG_DEFAULT_LEVEL_INFO=y
     # CONFIG_LOG_DEFAULT_LEVEL=3
     # CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
     # CONFIG_BOOTLOADER_LOG_LEVEL=3
     ```
3. **Cập nhật `sdkconfig` để tắt UART:**
   - Cài đặt:
     ```ini
     CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
     CONFIG_BOOTLOADER_LOG_LEVEL=0
     CONFIG_LOG_BOOTLOADER_LEVEL_NONE=y
     CONFIG_LOG_BOOTLOADER_LEVEL=0
     CONFIG_LOG_DEFAULT_LEVEL_NONE=y
     CONFIG_LOG_DEFAULT_LEVEL=0
     CONFIG_LOG_MAXIMUM_LEVEL=0
     CONFIG_ESP_CONSOLE_NONE=y
     CONFIG_ESP_CONSOLE_UART_NUM=-1
     CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1
     CONFIG_CONSOLE_UART_NONE=y
     CONFIG_ESP_CONSOLE_UART_NONE=y
     CONFIG_CONSOLE_UART_NUM=-1
     ```
4. **Cập nhật `build/CMakeCache.txt` (nếu tồn tại):**
   - Đặt `SDKCONFIG_DEFAULTS` thành `sdkconfig.defaults;sdkconfig.release`.
5. **Khôi phục firmware chính (`main/CMakeLists.txt`):**
   - Nếu đang ở chế độ component test, copy `main/CMakeLists.txt.bak` về `main/CMakeLists.txt`.
6. **Reconfigure và Build:**
   - Chạy `idf.py reconfigure` và `idf.py build`.
   - Báo cáo kết quả build release (tắt UART) hoàn tất cho người dùng.

---

## Custom Workflow: "bật uart" / Debug Test Mode
Khi người dùng yêu cầu bật lại UART / mở log / chạy test:
1. Đổi tên `sdkconfig.release` thành `sdkconfig.release.disabled`.
2. Thêm vào `sdkconfig.defaults`:
   ```ini
   CONFIG_ESP_CONSOLE_UART_DEFAULT=y
   CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
   CONFIG_LOG_DEFAULT_LEVEL_INFO=y
   CONFIG_LOG_DEFAULT_LEVEL=3
   CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
   CONFIG_BOOTLOADER_LOG_LEVEL=3
   ```
3. Cập nhật `sdkconfig` kích hoạt UART0 console và log INFO (level 3).
4. Reconfigure và build lại.
