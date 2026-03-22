# Component Test Program (Hardware Test Mode)

## Overview

A standalone test firmware, **completely separate from the main application**.
Purpose: test each hardware component on the board via UART commands (Serial Monitor).

When this firmware is flashed, the ESP32 **does not run any main application tasks**.
It only listens for commands on UART0 (USB Serial) and executes the corresponding test.

---

## Prerequisites

### Hardware required
- ESP32 board (hydro-logger-2) connected via USB
- Modules physically present on the board for each test:

| Test | Required hardware |
|------|-------------------|
| `test rtc` | PCF8563 RTC on I2C bus (SCL=GPIO22, SDA=GPIO21) |
| `test adc` | Voltage source connected to GPIO39 |
| `test laser` | Laser rangefinder on UART1 (TX=GPIO17, RX=GPIO16, PWR=GPIO26) |
| `test ultrasonic` | Ultrasonic sensor on UART1 (TX=GPIO17, RX=GPIO16, PWR=GPIO32) |
| `test sim` | SIM 4G module on UART2 (TX=GPIO5, RX=GPIO18, PWR=GPIO27) with SIM card inserted |
| `test dcom` | DCOM Wi-Fi module (PWR=GPIO23) within range of configured Wi-Fi AP |
| `test gpio` | LED on GPIO25 (visual inspection required) |
| `test i2c` | Any I2C device on the bus |
| `test timesync` | Working internet (SIM or Wi-Fi) + RTC on I2C |

### Software required
- ESP-IDF v5.5.x installed and configured
- Serial port identified (e.g., COM3, COM5)

### External dependencies

| Dependency | Value | Used by |
|------------|-------|---------|
| SIM APN | `v-internet` (Viettel) | `test sim`, `test timesync` |
| Wi-Fi SSID | Configured in `common/config.hpp` (`kDcomWifiSsid`) | `test dcom`, `test timesync` |
| Wi-Fi Password | Configured in `common/config.hpp` (`kDcomWifiPassword`) | `test dcom`, `test timesync` |
| Time sync URL | `http://donuoctrieuduong.xyz/dev_test/get_time.php` | `test timesync` |

> **Note:** If your SIM uses a different APN (e.g., Vinaphone `m3-world`), search for
> the string `"v-internet"` in `test_sim.cpp` and in `simConnect()` inside `test_timesync.cpp`,
> then replace it with your APN.

---

## How it works

```
+------------------+          UART0 (115200)          +------------------+
|  PC / Terminal   |  ------>  ESP32 Test Firmware     |
|  (Serial Monitor)|  <------  (test results)         |
+------------------+                                   +------------------+
```

### Main processing flow

1. **Boot**: ESP32 starts, initializes GPIO, UART, I2C, ADC (basic drivers)
2. **Menu**: Prints the list of available test commands
3. **Wait**: Reads UART0, waits for user input
4. **Execute**: When a valid command is received, runs **only that component's test**
5. **Result**: Prints PASS/FAIL to Serial Monitor
6. **Return to step 3**: Waits for the next command

### Important
- Each test command runs **only one component** at a time
- Other components are **not affected** (power off, no communication)
- After the test completes, the component is **powered off**
- No FreeRTOS tasks run in the background — everything is sequential in the main loop

---

## Command list

| Command | Description |
|---------|-------------|
| `help` | Show the command menu |
| `test rtc` | Test RTC PCF8563 (read time via I2C) |
| `test adc` | Test ADC (read voltage on GPIO39) |
| `test laser` | Test Laser sensor (power on, handshake, read 3 times) |
| `test ultrasonic` | Test Ultrasonic sensor (power on, Modbus read) |
| `test sim` | Test SIM 4G (power on, AT commands, network check) |
| `test dcom` | Test DCOM Wi-Fi (power on, connect to AP, get IP) |
| `test gpio` | Test GPIO outputs (LED blink, power pins toggle) |
| `test i2c` | Test I2C bus scan (find all devices on the bus) |
| `test timesync` | Sync RTC from server time (connect, HTTP GET, compare, update if delta > 60s) |
| `test all` | Run all tests sequentially (rtc, adc, i2c, gpio, laser, ultrasonic, sim, dcom, timesync) |
| `reboot` | Restart ESP32 |

---

## PASS / FAIL criteria

| Test | PASS condition | FAIL condition |
|------|----------------|----------------|
| `test rtc` | I2C read succeeds and returns a valid DateTime | I2C read fails (no ACK from PCF8563) |
| `test adc` | Average of 5 readings > 0 mV | All readings are 0 mV |
| `test laser` | Handshake ACK received (header 0xAA) and at least one distance reading returned | No ACK after 3 retries, or UART init fails |
| `test ultrasonic` | Modbus response with valid CRC received (15 bytes) | No response, CRC mismatch, or response too short |
| `test sim` | AT handshake OK + SIM card detected (AT+CPIN? -> READY) + network attach OK | AT timeout, no SIM card, or network attach fails |
| `test dcom` | Wi-Fi connected and IP address obtained within 30s | Connection timeout or Wi-Fi stack init fails |
| `test gpio` | Always PASS (visual: LED blinks 5 times, power pins toggle). **Operator must visually confirm LED blink.** | N/A — no automated FAIL for GPIO |
| `test i2c` | At least 1 device found on the I2C bus (0x08–0x77) | No devices found |
| `test timesync` | Server time fetched, parsed, and delta calculated. RTC updated if delta > 60s, or confirmed accurate if delta <= 60s | Cannot connect, HTTP GET fails, parse error, or setTime fails |

> **Note on ADC:** The raw mV value is printed but no specific voltage threshold is enforced.
> The test verifies the ADC peripheral is functional, not that a specific voltage is present.

> **Note on GPIO:** This test requires **visual inspection**. Watch the LED on GPIO25 — it should
> blink 5 times (500ms on/off). The serial log confirms pin state changes.

---

## Example output

```
> test sim
[TEST-SIM] === START ===
[TEST-SIM] Init UART2 baud=115200
[TEST-SIM] Power ON (HIGH -> LOW edge)
[TEST-SIM] Wait boot 12000ms...
[TEST-SIM] AT handshake OK
[TEST-SIM] AT> ATE0
[TEST-SIM] AT> AT+CPIN?
[TEST-SIM]   < +CPIN: READY
[TEST-SIM] SIM card: READY
[TEST-SIM] AT> AT+CSQ
[TEST-SIM] AT> AT+CGATT=1
[TEST-SIM] Network attach: OK
[TEST-SIM] Power OFF
[TEST-SIM] === PASS ===
```

---

## File structure

```
test/component_test/                 # Source code
├── CMakeLists.txt                    # Component CMake (replaces main/CMakeLists.txt when building)
├── test_common.hpp                   # Shared utilities (PASS/FAIL macros)
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
└── README.md                         # This file
```

---

## Build and flash (Windows / PowerShell)

### Step 1: Copy test sources into main/ and replace CMakeLists

```powershell
# Backup the original CMakeLists.txt
Copy-Item main\CMakeLists.txt main\CMakeLists.txt.bak

# Copy test sources into main/
Copy-Item -Recurse test\component_test main\component_test

# Replace main/CMakeLists.txt with the test version
Copy-Item test\component_test\CMakeLists.txt main\CMakeLists.txt
```

> **Note:** The root `CMakeLists.txt` does NOT need to be changed.
> Only `main/CMakeLists.txt` is replaced.

### Step 2: Build

```powershell
idf.py fullclean
idf.py build
```

### Step 3: Flash and open monitor

```powershell
idf.py -p COM3 flash monitor
```

Replace `COM3` with your actual serial port.

### Step 4: Type commands in the Serial Monitor (115200 baud)

### Restore main firmware

```powershell
# Restore original CMakeLists.txt
Copy-Item main\CMakeLists.txt.bak main\CMakeLists.txt

# Remove test sources from main/
Remove-Item -Recurse -Force main\component_test

# Rebuild
idf.py fullclean
idf.py build
```

---

## Technical notes

1. **UART0** (USB) is used for receiving commands and printing results. Baud rate: 115200
2. **UART1** (GPIO16/17) is used for sensors (laser/ultrasonic) — initialized only when needed
3. **UART2** (GPIO5/18) is used for SIM 4G — initialized only when needed
4. **I2C0** (GPIO21/22) is used for RTC PCF8563
5. All GPIO power pins are initialized to OFF (safe) at boot
6. Each test manages its own power on/off cycle
7. No FreeRTOS tasks run in the background — all tests run sequentially in the main loop
8. **Laser and ultrasonic share UART1** — do not run both simultaneously. `test all` runs them sequentially

---

## Adding a new test

1. Create `test_<component>.cpp` in `test/component_test/`
2. Implement `void test_<component>()`
3. Declare `extern void test_<component>()` in `test_main.cpp`
4. Add command to the dispatcher and menu in `test_main.cpp`
5. Add the source file to `test/component_test/CMakeLists.txt` SRCS list
