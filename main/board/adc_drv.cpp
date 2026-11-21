#include "adc_drv.hpp"
#include "board/pins.hpp"
#include "common/config.hpp"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char* TAG = "AdcDrv";
static adc_oneshot_unit_handle_t s_unit{};
static adc_channel_t s_chan{};          // battery voltage (GPIO34 / ADC1_CH6)
static adc_channel_t s_lsignal_chan{};  // analog pressure (optional)

bool AdcDrv::init() {
  adc_oneshot_unit_init_cfg_t cfg = {
    .unit_id = ADC_UNIT_1,
    .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    .ulp_mode = ADC_ULP_MODE_DISABLE
  };
  if (adc_oneshot_new_unit(&cfg, &s_unit) != ESP_OK) return false;

  // Battery voltage channel (GPIO 34 / ADC1_CH6)
  s_chan = (adc_channel_t)pins::ADC_CHAN;

  adc_oneshot_chan_cfg_t ccfg = {
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_DEFAULT
  };
  if (adc_oneshot_config_channel(s_unit, s_chan, &ccfg) != ESP_OK) return false;

  // L_SIGNAL channel for analog pressure sensor (if configured)
  if (pins::L_SIGNAL != GPIO_NUM_NC && pins::L_SIGNAL_ADC_CHAN >= 0) {
    s_lsignal_chan = (adc_channel_t)pins::L_SIGNAL_ADC_CHAN;
    if (adc_oneshot_config_channel(s_unit, s_lsignal_chan, &ccfg) != ESP_OK) return false;
  }

  ESP_LOGI(TAG, "ADC init ok");
  return true;
}

int AdcDrv::readMilliVolts() {
  if (!s_unit) return 0;
  int raw = 0;
  if (adc_oneshot_read(s_unit, s_chan, &raw) != ESP_OK) return 0;
  // Placeholder conversion; for accurate mV use calibration.
  return raw;
}

int AdcDrv::readLSignalRaw() {
  if (!s_unit) return 0;
  long sum = 0;
  int count = 0;
  for (int i = 0; i < cfg::kPressureAdcSamples; ++i) {
    int raw = 0;
    if (adc_oneshot_read(s_unit, s_lsignal_chan, &raw) == ESP_OK) {
      sum += raw;
      ++count;
    }
  }
  if (count == 0) return 0;
  return (int)(sum / count);
}
