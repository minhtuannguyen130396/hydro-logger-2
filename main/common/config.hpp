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
static constexpr uint32_t kConnCheckTimeoutMs = 60000;
static constexpr uint32_t kSyncWindowMs = 60000;
static constexpr uint32_t kQueuePopTimeoutMs = 200;
static constexpr uint32_t kSimPowerEdgeDelayMs = 100;
static constexpr uint32_t kSimBootDelayMs = 10000;
static constexpr uint32_t kSimAtHandshakeTimeoutMs = 12000;
static constexpr uint32_t kSimAtRetryDelayMs = 500;
static constexpr uint32_t kSimNetworkTimeoutMs = 40000;  // total budget for phase 3 (network readiness)
static constexpr uint32_t kSimRegPollIntervalMs = 2000;  // poll interval for CEREG/CGATT
static constexpr uint32_t kSimHttpDataTimeoutMs = 10000;
static constexpr uint32_t kSimHttpActionTimeoutMs = 30000;
static constexpr const char* kSimApnViettel = "v-internet";
static constexpr const char* kSimApnVinaphone = "m3-world";
static constexpr uint32_t kDcomPowerEdgeDelayMs = 100;
static constexpr const char* kDcomWifiSsid = "Minh Tuan";
static constexpr const char* kDcomWifiPassword = "j12345678";

// Queues
static constexpr int kMeasureQueueLen = 10;
static constexpr int kLogQueueLen     = 20;

// Logging
static constexpr int kSessionLogSize = 1024;

// Notify
static constexpr uint32_t kNotifyNormalMs = 1000;
static constexpr uint32_t kNotifyUrgentMs = 500;

// Device — compile-time default; runtime serial loaded from NVS
static constexpr const char* kDeviceSerialPrefix = "TD_MW_";
static constexpr uint16_t kDefaultDeviceCode = 12;

// Diagnostic (boot-time self-test)
static constexpr uint32_t kDiagnosticDelayMs = 10000;

// Boot config portal (Wi-Fi AP + HTTP)
static constexpr const char* kPortalApSsid   = "TRAM_DO_NUOC";
static constexpr uint32_t kPortalInactivityTimeoutMs = 120000;  // 2 minutes
static constexpr uint32_t kDiagTimeFetchIntervalMs   = 10000;   // 10 seconds
static constexpr uint32_t kDiagTimeFetchWindowMs     = 120000;  // 2 minutes total

// OTA
static constexpr int kOtaMaxAttempts         = 3;   // max full download retries
static constexpr uint32_t kOtaHttpTimeoutMs  = 15000;
static constexpr const char* kCurrentFwVersion = "1.1.0"; // hardcoded current version

#if defined(TEST_OTA)
static constexpr const char* kFirmwareVersionUrl = "https://test.example.com/fw/version.json";
static constexpr const char* kFirmwareBinUrl     = "https://test.example.com/fw/firmware.bin";
#else 
static constexpr const char* kFirmwareVersionUrl = "https://example.com/fw/version.json";
static constexpr const char* kFirmwareBinUrl     = "https://example.com/fw/firmware.bin";
#endif

} // namespace cfg
