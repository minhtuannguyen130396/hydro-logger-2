#pragma once
#include <cstdint>

#ifndef CFG_SYNC_SEND_RETRIES
#define CFG_SYNC_SEND_RETRIES 3
#endif

namespace cfg {

// Scheduling

static constexpr int kScheduleMinutes[6] = {0,10,20,30,40,50};

// Max time the device stays awake in one wake cycle before forcing deep sleep.
// Must exceed the worst-case sync duration including a full OTA download
// (warmup + time sync 20s + OTA download up to 15 min + send window 60s).
static constexpr uint32_t kAwakeBudgetMs = 1080000;  // 18 minutes

// Extended awake cap used while an OTA download is in progress. Deep-sleeping
// mid-download resets the chip and aborts the update, so the scheduler must
// keep the device awake until OTA finishes. Sized to cover the pre-OTA sync
// work (~3 min) plus a full download attempt (kOtaSimTotalTimeoutMs, 15 min)
// with margin; it still bounds a stalled modem so we can't stay awake forever.
static constexpr uint32_t kOtaAwakeBudgetMs = 1200000;  // 20 min

// Measurement
static constexpr int kDistanceSamples = 3;
static constexpr int kMaxDistanceMm  = 30000;   // max ultrasonic range ~30m
static constexpr int kMaxRepeatRead  = 5;       // MAXIMUM_TIMES_REPEAT

// Pressure sensor (analog, read from L_SIGNAL / ADC1_CH7)
// Each reading averages this many raw ADC samples for noise rejection.
static constexpr int kPressureAdcSamples = 16;
static constexpr uint32_t kSensorWarmupMs = 1500;
static constexpr uint32_t kSensorHandshakeTimeoutMs = 800;
static constexpr int kSensorHandshakeRetries = 3;

// Sync
static constexpr uint32_t kConnCheckTimeoutMs = 60000;
static constexpr uint32_t kSyncWindowMs = 60000;
static constexpr uint32_t kQueuePopTimeoutMs = 200;
static constexpr int kSyncSendRetries = CFG_SYNC_SEND_RETRIES;
static constexpr uint32_t kSimPowerEdgeDelayMs = 100;
static constexpr uint32_t kSimBootDelayMs = 5000;
static constexpr uint32_t kSimAtHandshakeTimeoutMs = 12000;
static constexpr uint32_t kSimAtRetryDelayMs = 500;
static constexpr uint32_t kSimNetworkTimeoutMs = 40000;  // total budget for phase 3 (network readiness)
static constexpr uint32_t kSimRegPollIntervalMs = 2000;  // poll interval for CEREG/CGATT
static constexpr uint32_t kSimHttpDataTimeoutMs = 10000;
static constexpr uint32_t kSimHttpActionTimeoutMs = 30000;
static constexpr const char* kSimApnViettel = "v-internet";
static constexpr const char* kSimApnVinaphone = "m3-world";
static constexpr uint32_t kDcomPowerEdgeDelayMs = 100;
static constexpr uint32_t kDcomBootDelayMs = 500;
static constexpr const char* kDcomWifiSsid = "Minh Tuan";
static constexpr const char* kDcomWifiPassword = "j12345678";

// Queues
static constexpr int kMeasureQueueLen = 30;
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

// Boot diagnostic active window.
// After the boot self-test + connectivity bring-up, the device keeps the comm
// link up (Wi-Fi preferred, SIM fallback) for kDiagWindowMs and, every
// kDiagCycleIntervalMs, sends a water-level reading, syncs the RTC from the
// server, and runs the firmware-version/OTA check. If an OTA update is found the
// OTA flow takes over with its own timeout (kOtaSimTotalTimeoutMs, ...) and
// restarts the device on success, so this fixed window only bounds the no-OTA
// case.
static constexpr uint32_t kDiagWindowMs        = 180000;  // 3 minutes (no-OTA case)
static constexpr uint32_t kDiagCycleIntervalMs = 30000;   // 30s between cycles

// OTA
// Master on/off switch. Set false to temporarily disable the OTA check in the
// sync cycle (no version fetch, no download). Flip back to true to re-enable.
static constexpr bool kOtaEnabled            = true;
static constexpr int kOtaMaxAttempts         = 3;   // max full download retries
static constexpr uint32_t kOtaHttpTimeoutMs  = 15000;
static constexpr const char* kCurrentFwVersion = "ver1.1.16"; // must match server fw_version exactly

// OTA over SIM (4G modem). The firmware image is fetched in small HTTP Range
// windows: each window is its own GET (AT+HTTPACTION) carrying a
// "Range: bytes=A-B" header, then read out with AT+HTTPREAD. Windows are kept
// well under the modem's HTTP body-cache cap (~149 KB observed on A76XX: a
// whole-file GET + ranged HTTPREAD returns ERROR past offset ~152576), and each
// request is short enough that the server's keep-alive idle timeout never trips.
static constexpr int      kOtaSimWindowSize     = 32768;  // bytes per HTTP Range window (<< ~149K modem cap)
static constexpr int      kOtaSimChunkSize      = 8192;   // legacy: bytes per AT+HTTPREAD (old whole-file path)
static constexpr uint32_t kOtaSimReadTimeoutMs  = 8000;   // stall timeout while reading one chunk
static constexpr uint32_t kOtaSimTotalTimeoutMs = 900000; // hard cap per download attempt (15 min)

// Firmware OTA endpoints. Plain HTTP (served through the modem AT HTTP stack;
// the modem path does not configure TLS). Real links to be provided later.
#if defined(TEST_OTA)
static constexpr const char* kFirmwareVersionUrl = "http://test.example.com/fw/version.json";
static constexpr const char* kFirmwareBinUrl     = "http://test.example.com/fw/firmware.bin";
#else
static constexpr const char* kFirmwareVersionUrl = "http://donuoctrieuduong.xyz/hydro-logger-api/ota/ap-luc/version";
static constexpr const char* kFirmwareBinUrl     = "http://donuoctrieuduong.xyz/hydro-logger-api/ota/ap-luc/firmware.bin";
#endif

// Set true once kFirmwareVersionUrl / kFirmwareBinUrl point at a real host.
// While false the sync task skips the OTA check entirely (no wasted airtime).
static constexpr bool kOtaEndpointsConfigured = true;

} // namespace cfg
