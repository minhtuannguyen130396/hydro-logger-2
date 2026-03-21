# Component-level CMakeLists.txt for test firmware
#
# This replaces main/CMakeLists.txt when building the test program.
#
# To build test firmware:
#   1. Backup: cp main/CMakeLists.txt main/CMakeLists.txt.bak
#   2. Copy test sources into main/:
#      cp test/component_test/*.cpp test/component_test/*.hpp main/component_test/
#   3. Replace: cp test/component_test/CMakeLists.txt main/CMakeLists.txt
#   4. Build: idf.py build
#   5. Restore: cp main/CMakeLists.txt.bak main/CMakeLists.txt

idf_component_register(
  SRCS
    # Test program entry + tests
    "component_test/test_main.cpp"
    "component_test/test_rtc.cpp"
    "component_test/test_adc.cpp"
    "component_test/test_sensor.cpp"
    "component_test/test_sim.cpp"
    "component_test/test_dcom.cpp"
    "component_test/test_gpio.cpp"
    "component_test/test_i2c.cpp"
    "component_test/test_timesync.cpp"

    # Reuse existing board drivers (no changes needed)
    "board/gpio_drv.cpp"
    "board/uart_drv.cpp"
    "board/i2c_drv.cpp"
    "board/i2c_port.cpp"
    "board/adc_drv.cpp"

    # Reuse existing modules
    "modules/rtc/pcf8563.cpp"
    "modules/rtc/rtc_pcf8563.cpp"
    "modules/io/io_controller.cpp"

    # Common utilities
    "common/nvs_store.cpp"
    "common/time_utils.cpp"

  INCLUDE_DIRS
    "."
    "component_test"
    "board"
    "common"
    "modules"
    "modules/rtc"
    "modules/io"
    "modules/sensor"
    "services"
    "services/connectivity"
    "services/logging"
)

# C++ standard
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wno-missing-field-initializers)
