#include "adc_drv.hpp"
#include "board/pins.hpp"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char* TAG = "AdcDrv";
static adc_oneshot_unit_handle_t s_unit{};
static adc_channel_t s_chan{};

bool AdcDrv::init() {
  adc_oneshot_unit_init_cfg_t cfg = {
    .unit_id = ADC_UNIT_1,
    .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    .ulp_mode = ADC_ULP_MODE_DISABLE
  };
  if (adc_oneshot_new_unit(&cfg, &s_unit) != ESP_OK) return false;

  // NOTE: mapping pins::ADC_CHAN -> adc_channel_t is chip-dependent.
  // Here we assume ADC1 channel number equals pins::ADC_CHAN (placeholder).
  s_chan = (adc_channel_t)pins::ADC_CHAN;

  adc_oneshot_chan_cfg_t ccfg = {
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_DEFAULT
  };
  if (adc_oneshot_config_channel(s_unit, s_chan, &ccfg) != ESP_OK) return false;
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
