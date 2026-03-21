# Component Test Program (Hardware Test Mode)

## Tong quan

Chuong trinh test rieng biet, **tach hoan toan khoi luong chinh** cua hydro-logger-2.
Muc dich: test tung component tren mach thong qua lenh UART (Serial Monitor).

Khi flash firmware test nay, ESP32 se **khong chay bat ky task nao cua ung dung chinh**.
Thay vao do, no chi lang nghe lenh tu UART0 (USB Serial) va thuc thi test tuong ung.

---

## Cach hoat dong

```
+------------------+          UART0 (115200)          +------------------+
|  PC / Terminal   |  ------>  ESP32 Test Firmware     |
|  (Serial Monitor)|  <------  (ket qua test)         |
+------------------+                                   +------------------+
```

### Luong xu ly chinh

1. **Boot**: ESP32 khoi dong, init GPIO, UART, I2C, ADC (cac driver co ban)
2. **Menu**: In ra danh sach cac lenh test co san
3. **Cho lenh**: Doc UART0, cho nguoi dung nhap lenh
4. **Thuc thi**: Khi nhan lenh hop le -> chi chay test cho component do
5. **Ket qua**: In ket qua PASS/FAIL len Serial Monitor
6. **Quay lai buoc 3**: Cho lenh tiep theo

### Quan trong
- Moi lenh test chi chay **mot component duy nhat** tai mot thoi diem
- Cac component khac **khong bi anh huong** (power off, khong giao tiep)
- Sau khi test xong, component duoc **power off** (tru khi co lenh giu)

---

## Danh sach lenh

| Lenh            | Mo ta                                          |
|-----------------|-------------------------------------------------|
| `help`          | In danh sach lenh                               |
| `test rtc`      | Test RTC PCF8563 (doc thoi gian qua I2C)        |
| `test adc`      | Test ADC (doc dien ap GPIO39)                    |
| `test laser`    | Test Laser sensor (power on, handshake, doc 3 lan) |
| `test ultrasonic` | Test Ultrasonic sensor (power on, Modbus read)  |
| `test sim`      | Test SIM 4G (power on, AT command, kiem tra mang)|
| `test dcom`     | Test DCOM Wi-Fi (power on, ket noi Wi-Fi)        |
| `test gpio`     | Test GPIO outputs (LED blink, power pins toggle)  |
| `test i2c`      | Test I2C bus scan (tim tat ca device tren bus)    |
| `test timesync` | Sync RTC from server time (DCOM/SIM -> HTTP GET -> compare -> update) |
| `test all`      | Chay lan luot tat ca cac test                    |
| `reboot`        | Khoi dong lai ESP32                              |

### Vi du su dung

```
> help
========== COMPONENT TEST MENU ==========
  help           - Show this menu
  test rtc       - Test RTC PCF8563
  test adc       - Test ADC voltage
  test laser     - Test Laser sensor
  test ultrasonic - Test Ultrasonic sensor
  test sim       - Test SIM 4G module
  test dcom      - Test DCOM Wi-Fi
  test gpio      - Test GPIO outputs
  test i2c       - Test I2C bus scan
  test timesync  - Sync RTC from server time
  test all       - Run all tests
  reboot         - Restart ESP32
==========================================

> test sim
[TEST-SIM] === START ===
[TEST-SIM] Power ON (edge trigger HIGH->LOW)
[TEST-SIM] Wait boot 12000ms...
[TEST-SIM] AT -> OK
[TEST-SIM] ATE0 -> OK
[TEST-SIM] AT+CPIN? -> +CPIN: READY
[TEST-SIM] AT+CSQ -> +CSQ: 18,0 (signal OK)
[TEST-SIM] AT+CGATT=1 -> OK
[TEST-SIM] APN=v-internet -> OK
[TEST-SIM] === PASS ===
[TEST-SIM] Power OFF
```

---

## Cau truc file

```
test/component_test/                 # Source code
├── CMakeLists.txt                    # Component CMake (thay main/CMakeLists.txt khi build)
├── project_CMakeLists.txt            # Project-level CMake (thay root CMakeLists.txt)
├── test_common.hpp                   # Shared utilities (print PASS/FAIL macros)
├── test_main.cpp                     # Entry point + UART command dispatcher
├── test_rtc.cpp                      # Test RTC PCF8563
├── test_adc.cpp                      # Test ADC voltage
├── test_sensor.cpp                   # Test Laser + Ultrasonic sensor
├── test_sim.cpp                      # Test SIM 4G module
├── test_dcom.cpp                     # Test DCOM Wi-Fi
├── test_gpio.cpp                     # Test GPIO outputs (LED, power pins)
├── test_i2c.cpp                      # Test I2C bus scan
└── test_timesync.cpp                 # Sync RTC time from HTTP server

docs/test_case/                      # Documentation
└── README.md                         # File nay (huong dan hoat dong)
```

---

## Cach build va flash

### Buoc 1: Copy file test vao main/ va thay CMakeLists

```bash
# Backup CMakeLists.txt goc
cp main/CMakeLists.txt main/CMakeLists.txt.bak

# Copy source test vao main/
cp -r test/component_test main/component_test

# Thay CMakeLists.txt cua main
cp test/component_test/CMakeLists.txt main/CMakeLists.txt
```

### Buoc 2: Build

```bash
idf.py fullclean
idf.py build
```

### Buoc 3: Flash

```bash
idf.py -p COMx flash monitor
```

### Buoc 4: Mo Serial Monitor (115200 baud) va nhap lenh

### Khoi phuc ve firmware chinh

```bash
cp main/CMakeLists.txt.bak main/CMakeLists.txt
rm -rf main/component_test
idf.py fullclean && idf.py build
```

---

## Luu y ky thuat

1. **UART0** (USB) duoc dung de nhan lenh va in ket qua. Baud rate: 115200
2. **UART1** (GPIO16/17) duoc dung cho sensor (laser/ultrasonic) - chi init khi can
3. **UART2** (GPIO5/18) duoc dung cho SIM 4G - chi init khi can
4. **I2C0** (GPIO21/22) duoc dung cho RTC PCF8563
5. Tat ca GPIO power pins duoc init o trang thai OFF (safe) khi boot
6. Moi test tu quan ly power on/off cho component cua minh
7. Khong co FreeRTOS task nao chay ngam - tat ca deu chay tuan tu trong main loop

---

## Cach them test moi

1. Tao file `test_<component>.cpp` trong `test/component_test/`
2. Implement ham `void test_<component>()`
3. Khai bao `extern void test_<component>()` trong `test_main.cpp`
4. Them lenh vao command dispatcher trong `test_main.cpp`
5. Them vao `test/component_test/CMakeLists.txt` src list
