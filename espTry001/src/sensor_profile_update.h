#pragma once

#include <Arduino.h>
#include <FS.h>
#include "sensor_profile_store.h"

// USB 配置上传协议接收器。update() 采用流式接收，不会把整个 JSON 放入 RAM。
class SensorProfileUpdater {
public:
  enum class State : uint8_t {
    Idle,
    ReceivingHeader,
    ReceivingPayload,
    Success,
    Failed
  };

  // 清理旧临时文件并准备接收 ESPCFG1 协议帧。
  void begin(SensorProfileStore &store);
  // 每次最多处理一小批串口数据，必须在 loop() 中持续调用。
  void update(Stream &usb);
  void cancel();

  State state() const { return state_; }
  uint32_t receivedLength() const { return receivedLength_; }
  uint32_t expectedLength() const { return expectedLength_; }
  const char *statusText() const;
  const char *errorText() const { return errorText_; }
  bool shouldRestart() const;

private:
  static constexpr char kMagic[] = "ESPCFG1";
  static constexpr size_t kMagicLength = 7;
  static constexpr size_t kHeaderLength = kMagicLength + 4 + 4;
  static constexpr size_t kChunkSize = 128;
  static constexpr uint32_t kRestartDelayMs = 1200;
  static constexpr const char *kCurrentPath = "/sensor_profiles.json";
  static constexpr const char *kBackupPath = "/sensor_profiles.backup";
  static constexpr const char *kUploadingPath = "/sensor_profiles.uploading";

  void handleHeader(Stream &usb);
  void handlePayload(Stream &usb);
  bool installCandidate();
  void fail(Stream &usb, const char *message);
  static uint32_t readLe32(const uint8_t *data);
  static uint32_t updateCrc32(uint32_t crc, const uint8_t *data, size_t length);

  SensorProfileStore *store_ = nullptr;
  File uploadFile_;
  State state_ = State::Idle;
  uint8_t header_[kHeaderLength] = {0};
  size_t headerLength_ = 0;
  uint32_t expectedLength_ = 0;
  uint32_t expectedCrc32_ = 0;
  uint32_t runningCrc32_ = 0xFFFFFFFF;
  uint32_t receivedLength_ = 0;
  uint32_t successTimeMs_ = 0;
  char errorText_[96] = {0};
};
