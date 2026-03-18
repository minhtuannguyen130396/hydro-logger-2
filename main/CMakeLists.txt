idf_component_register(
  SRCS
    "app/app_main.cpp"
    "app/scheduler_task.cpp"
    "app/measure_task.cpp"
    "app/sync_task.cpp"
    "app/notify_task.cpp"
    "app/ota_task.cpp"

    "middleware/message_bus.cpp"
    "middleware/publish_api.cpp"

    "services/connectivity/sim4g_module.cpp"
    "services/connectivity/dcom_module.cpp"
    "services/connectivity/connectivity_manager.cpp"
    "services/net/http_client.cpp"
    "services/net/server_api.cpp"
    "services/pack/json_packer.cpp"
    "services/logging/log_buffer.cpp"
    "services/logging/log_service.cpp"
    "services/power/power_manager.cpp"
    "services/ota/ota_service.cpp"

    "modules/sensor/laser_sensor.cpp"
    "modules/sensor/ultrasonic_sensor.cpp"
    "modules/sensor/sensor_manager.cpp"
    "modules/rtc/pcf8563.cpp"
    "modules/rtc/rtc_pcf8563.cpp"
    "modules/io/io_controller.cpp"

    "board/gpio_drv.cpp"
    "board/uart_drv.cpp"
    "board/i2c_drv.cpp"
    "board/i2c_port.cpp"
    "board/adc_drv.cpp"

    "common/nvs_store.cpp"
    "common/time_utils.cpp"
  INCLUDE_DIRS
    "."
    "app"
    "middleware"
    "services"
    "services/connectivity"
    "services/net"
    "services/pack"
    "services/logging"
    "services/power"
    "services/ota"
    "modules"
    "modules/sensor"
    "modules/rtc"
    "modules/io"
    "board"
    "common"
)

# C++ standard
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wno-missing-field-initializers)

# OTA build profiles: -DTEST_OTA or -DDEVELOP_OTA
# Set via: idf.py build -DCMAKE_C_FLAGS="-DTEST_OTA" -DCMAKE_CXX_FLAGS="-DTEST_OTA"
# Or uncomment one of the lines below:
# target_compile_definitions(${COMPONENT_LIB} PRIVATE TEST_OTA)
# target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVELOP_OTA)
