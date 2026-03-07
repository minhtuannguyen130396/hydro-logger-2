#pragma once
#include <cstdint>

namespace cfg {

// Scheduling
static constexpr int kScheduleMinutes[6] = {0,10,20,30,40,50};

// Measurement
static constexpr int kDistanceSamples = 3;
static constexpr int kMaxDistanceMm  = 10000;   // adjust
static constexpr int kMaxRepeatRead  = 5;       // MAXIMUM_TIMES_REPEAT
static constexpr uint32_t kSensorWarmupMs = 1500;
static constexpr uint32_t kSensorHandshakeTimeoutMs = 800;
static constexpr int kSensorHandshakeRetries = 3;

// Sync
static constexpr uint32_t kConnCheckTimeoutMs = 12000;
static constexpr uint32_t kSyncWindowMs = 60000;
static constexpr uint32_t kQueuePopTimeoutMs = 200;

// Queues
static constexpr int kMeasureQueueLen = 10;
static constexpr int kLogQueueLen     = 20;

// Logging
static constexpr int kSessionLogSize = 1024;

// Notify
static constexpr uint32_t kNotifyNormalMs = 1000;
static constexpr uint32_t kNotifyUrgentMs = 500;

// OTA
static constexpr const char* kFirmwareVersionUrl = "https://example.com/fw/version.json";
static constexpr const char* kFirmwareBinUrl     = "https://example.com/fw/firmware.bin";

} // namespace cfg
