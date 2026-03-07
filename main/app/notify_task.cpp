#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"

extern "C" void notify_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  bool led = false;
  bool spk = false;

  while (true) {
    auto bits = ctx->state.get();

    const bool urgent = (bits & AppState::BIT_CONN_FAIL) != 0;
    const uint32_t period = urgent ? cfg::kNotifyUrgentMs : cfg::kNotifyNormalMs;

    led = !led;
    IoController::instance().setLed(led);

    if (urgent) {
      spk = !spk;
      IoController::instance().setSpeaker(spk);
    } else {
      spk = false;
      IoController::instance().setSpeaker(false);
    }

    vTaskDelay(pdMS_TO_TICKS(period));
  }
}
