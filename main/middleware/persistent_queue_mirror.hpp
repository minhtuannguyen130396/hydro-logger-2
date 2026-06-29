#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "middleware/data_models.hpp"

namespace persistent_queue {

static constexpr const char* kMountPoint = "/storage";
static constexpr uint32_t kSnapshotMagic = 0x51554D52;  // "QUMR"
static constexpr uint32_t kSnapshotVersion = 1;

inline bool ensureStorageMounted() {
  static bool mounted = false;
  static bool attempted = false;

  if (attempted) return mounted;
  attempted = true;

  esp_vfs_spiffs_conf_t conf{};
  conf.base_path = kMountPoint;
  conf.partition_label = "storage";
  conf.max_files = 4;
  conf.format_if_mount_failed = true;

  const esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE("PersistentQueue", "SPIFFS mount failed: %s", esp_err_to_name(err));
    mounted = false;
    return false;
  }

  mounted = true;
  return true;
}

struct SnapshotHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t item_count;
  uint32_t item_size;
};

struct DeviceMetaRecord {
  char device_id[32];
  char fw_version[16];
  int voltage_mv;
};

struct MeasurementRecord {
  DateTime time;
  int dist_mm[cfg::kDistanceSamples];
  uint8_t valid;
  DeviceMetaRecord meta;
};

struct LogRecord {
  DateTime time;
  char text[cfg::kSessionLogSize];
  uint16_t len;
  DeviceMetaRecord meta;
};

template <typename T>
struct RecordCodec;

template <>
struct RecordCodec<MeasurementMsg> {
  using Record = MeasurementRecord;

  static Record encode(const MeasurementMsg& msg) {
    Record rec{};
    rec.time = msg.time;
    for (int i = 0; i < cfg::kDistanceSamples; ++i) rec.dist_mm[i] = msg.dist_mm[i];
    rec.valid = msg.valid ? 1 : 0;
    std::memcpy(rec.meta.device_id, msg.meta.device_id, sizeof(rec.meta.device_id));
    std::memcpy(rec.meta.fw_version, msg.meta.fw_version, sizeof(rec.meta.fw_version));
    rec.meta.voltage_mv = msg.meta.voltage_mv;
    return rec;
  }

  static MeasurementMsg decode(const Record& rec) {
    MeasurementMsg msg{};
    msg.time = rec.time;
    for (int i = 0; i < cfg::kDistanceSamples; ++i) msg.dist_mm[i] = rec.dist_mm[i];
    msg.valid = rec.valid != 0;
    std::memcpy(msg.meta.device_id, rec.meta.device_id, sizeof(msg.meta.device_id));
    std::memcpy(msg.meta.fw_version, rec.meta.fw_version, sizeof(msg.meta.fw_version));
    msg.meta.voltage_mv = rec.meta.voltage_mv;
    return msg;
  }
};

template <>
struct RecordCodec<LogMsg> {
  using Record = LogRecord;

  static Record encode(const LogMsg& msg) {
    Record rec{};
    rec.time = msg.time;
    std::memcpy(rec.text, msg.text, sizeof(rec.text));
    rec.len = msg.len;
    std::memcpy(rec.meta.device_id, msg.meta.device_id, sizeof(rec.meta.device_id));
    std::memcpy(rec.meta.fw_version, msg.meta.fw_version, sizeof(rec.meta.fw_version));
    rec.meta.voltage_mv = msg.meta.voltage_mv;
    return rec;
  }

  static LogMsg decode(const Record& rec) {
    LogMsg msg{};
    msg.time = rec.time;
    std::memcpy(msg.text, rec.text, sizeof(msg.text));
    msg.len = rec.len;
    std::memcpy(msg.meta.device_id, rec.meta.device_id, sizeof(msg.meta.device_id));
    std::memcpy(msg.meta.fw_version, rec.meta.fw_version, sizeof(msg.meta.fw_version));
    msg.meta.voltage_mv = rec.meta.voltage_mv;
    return msg;
  }
};

template <typename T>
class PersistentQueueMirror {
public:
  PersistentQueueMirror(const char* path, size_t capacity)
      : path_(path), capacity_(capacity) {}

  bool init() {
    if (!ensureStorageMounted()) return false;
    return loadSnapshot();
  }

  bool restoreToRam(QueueHandle_t q) {
    if (!q) return false;

    size_t restored = 0;
    for (const auto& item : shadow_) {
      if (xQueueSend(q, &item, 0) != pdTRUE) break;
      ++restored;
    }

    if (restored != shadow_.size()) {
      shadow_.resize(restored);
      return persistSnapshot();
    }
    return true;
  }

  bool enqueue(QueueHandle_t q, const T& item) {
    if (!q || xQueueSend(q, &item, 0) != pdTRUE) return false;

    if (shadow_.size() < capacity_) {
      shadow_.push_back(item);
    } else {
      ESP_LOGW("PersistentQueue", "shadow queue already full for %s", path_);
    }
    return persistSnapshot();
  }

  bool peek(QueueHandle_t q, T& out, uint32_t timeoutMs) const {
    return q && xQueuePeek(q, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }

  bool pop(QueueHandle_t q, T& out, uint32_t timeoutMs) {
    if (!q || xQueueReceive(q, &out, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;
    if (!shadow_.empty()) shadow_.erase(shadow_.begin());
    return persistSnapshot();
  }

  bool ack(QueueHandle_t q) {
    T dropped{};
    if (!q || xQueueReceive(q, &dropped, 0) != pdTRUE) return false;
    if (!shadow_.empty()) shadow_.erase(shadow_.begin());
    return persistSnapshot();
  }

private:
  bool loadSnapshot() {
    shadow_.clear();

    FILE* f = std::fopen(path_, "rb");
    if (!f) return true;

    SnapshotHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1 ||
        hdr.magic != kSnapshotMagic ||
        hdr.version != kSnapshotVersion ||
        hdr.item_size != sizeof(typename RecordCodec<T>::Record)) {
      std::fclose(f);
      ESP_LOGW("PersistentQueue", "snapshot header invalid for %s", path_);
      return false;
    }

    shadow_.reserve(hdr.item_count);
    for (uint32_t i = 0; i < hdr.item_count && shadow_.size() < capacity_; ++i) {
      typename RecordCodec<T>::Record rec{};
      if (std::fread(&rec, sizeof(rec), 1, f) != 1) break;
      shadow_.push_back(RecordCodec<T>::decode(rec));
    }

    std::fclose(f);
    return true;
  }

  bool persistSnapshot() const {
    FILE* f = std::fopen(path_, "wb");
    if (!f) {
      ESP_LOGE("PersistentQueue", "open snapshot fail: %s", path_);
      return false;
    }

    SnapshotHeader hdr{};
    hdr.magic = kSnapshotMagic;
    hdr.version = kSnapshotVersion;
    hdr.item_count = static_cast<uint32_t>(shadow_.size());
    hdr.item_size = sizeof(typename RecordCodec<T>::Record);

    bool ok = std::fwrite(&hdr, sizeof(hdr), 1, f) == 1;
    for (size_t i = 0; ok && i < shadow_.size(); ++i) {
      const auto rec = RecordCodec<T>::encode(shadow_[i]);
      ok = std::fwrite(&rec, sizeof(rec), 1, f) == 1;
    }

    std::fflush(f);
    std::fclose(f);
    if (!ok) {
      ESP_LOGE("PersistentQueue", "write snapshot fail: %s", path_);
    }
    return ok;
  }

  const char* path_;
  size_t capacity_;
  std::vector<T> shadow_;
};

}  // namespace persistent_queue
