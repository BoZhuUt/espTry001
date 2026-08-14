#pragma once

#include <Arduino.h>
#include <FS.h>
#include "calibration_profile_store.h"
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
  void attachCalibrationStore(CalibrationProfileStore &store) {
    calibrationStore_ = &store;
  }
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
  enum class PayloadType : uint8_t {
    None,
    SensorProfiles,
    CalibrationPackage,
  };

  static constexpr char kSensorMagic[] = "ESPCFG1";
  static constexpr char kCalibrationMagic[] = "ESPCAL1";
  static constexpr char kPackageMagic[] = "CALPKG1";
  static constexpr size_t kMagicLength = 7;
  static constexpr size_t kHeaderLength = kMagicLength + 4 + 4;
  static constexpr size_t kChunkSize = 128;
  static constexpr size_t kPackageEntryHeaderLength = 10;
  static constexpr uint32_t kMaxCalibrationPackageSize = 512 * 1024;
  static constexpr uint32_t kRestartDelayMs = 1200;
  static constexpr const char *kCurrentPath = "/sensor_profiles.json";
  static constexpr const char *kBackupPath = "/sensor_profiles.backup";
  static constexpr const char *kUploadingPath = "/sensor_profiles.uploading";
  static constexpr const char *kCalibrationPackagePath =
      "/calibration.package.uploading";
  static constexpr const char *kCalibrationIndexPath =
      "/calibration/index.json";
  static constexpr const char *kCalibrationIndexBackupPath =
      "/calibration/index.backup";
  static constexpr const char *kCalibrationIndexUploadingPath =
      "/calibration/index.uploading";
  static constexpr const char *kCalibrationProfileUploadingPath =
      "/calibration/profile.uploading";

  void handleHeader(Stream &usb);
  void handlePayload(Stream &usb);
  bool installCandidate();
  bool installCalibrationPackage();
  bool unpackCalibrationPackage();
  bool commitCalibrationIndex();
  bool copyPackageEntry(File &package, const char *path, uint32_t length,
                        uint32_t expectedCrc, bool indexEntry);
  bool readExact(File &file, uint8_t *buffer, size_t length);
  bool validCalibrationProfilePath(const char *path) const;
  void fail(Stream &usb, const char *message);
  static uint32_t readLe32(const uint8_t *data);
  static uint32_t updateCrc32(uint32_t crc, const uint8_t *data, size_t length);

  SensorProfileStore *store_ = nullptr;
  CalibrationProfileStore *calibrationStore_ = nullptr;
  File uploadFile_;
  PayloadType payloadType_ = PayloadType::None;
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
