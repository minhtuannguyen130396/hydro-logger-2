#pragma once
#include "common/singleton.hpp"
#include "modules/sensor/sensor.hpp"
#include "common/nvs_store.hpp"
#include "common/config.hpp"

class SensorManager : public Singleton<SensorManager> {
  friend class Singleton<SensorManager>;
public:
  bool ensureReady(ISensor*& outActive, LogBuffer& log);
  bool read3(ISensor* active, int out[cfg::kDistanceSamples], LogBuffer& log);

private:
  SensorManager() = default;

  ISensor* selectPreferred();
  ISensor* other(ISensor* s);
  void updateLastWorking(ISensor* s);
  bool isValidDistance(int mm) const;
};
