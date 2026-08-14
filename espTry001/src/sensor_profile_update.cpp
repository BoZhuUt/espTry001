#include "sensor_profile_update.h"

#include <LittleFS.h>

constexpr char SensorProfileUpdater::kSensorMagic[];
constexpr char SensorProfileUpdater::kCalibrationMagic[];
constexpr char SensorProfileUpdater::kPackageMagic[];
constexpr const char *SensorProfileUpdater::kCurrentPath;
constexpr const char *SensorProfileUpdater::kBackupPath;
constexpr const char *SensorProfileUpdater::kUploadingPath;
constexpr const char *SensorProfileUpdater::kCalibrationPackagePath;
constexpr const char *SensorProfileUpdater::kCalibrationIndexPath;
constexpr const char *SensorProfileUpdater::kCalibrationIndexBackupPath;
constexpr const char *SensorProfileUpdater::kCalibrationIndexUploadingPath;
constexpr const char *SensorProfileUpdater::kCalibrationProfileUploadingPath;

namespace {

void removeIfExists(const char *path) {
  if (LittleFS.exists(path)) LittleFS.remove(path);
}

}  // namespace

void SensorProfileUpdater::begin(SensorProfileStore &store) {
  cancel();
  store_ = &store;
  removeIfExists(kUploadingPath);
  removeIfExists(kCalibrationPackagePath);
  removeIfExists(kCalibrationIndexUploadingPath);
  removeIfExists(kCalibrationProfileUploadingPath);
  state_ = State::ReceivingHeader;
}

void SensorProfileUpdater::update(Stream &usb) {
  // Keep CONFIG mode usable after a rejected request. The PC closes and
  // reopens the port for its next attempt; the first arriving byte starts a
  // fresh ESPCFG1 header without requiring the operator to leave the page.
  if (state_ == State::Failed && usb.available()) {
    headerLength_ = 0;
    expectedLength_ = 0;
    expectedCrc32_ = 0;
    runningCrc32_ = 0xFFFFFFFF;
    receivedLength_ = 0;
    payloadType_ = PayloadType::None;
    errorText_[0] = '\0';
    state_ = State::ReceivingHeader;
  }
  if (state_ == State::ReceivingHeader) {
    handleHeader(usb);
  } else if (state_ == State::ReceivingPayload) {
    handlePayload(usb);
  }
}

void SensorProfileUpdater::cancel() {
  if (uploadFile_) uploadFile_.close();
  removeIfExists(kUploadingPath);
  removeIfExists(kCalibrationPackagePath);
  removeIfExists(kCalibrationIndexUploadingPath);
  removeIfExists(kCalibrationProfileUploadingPath);
  state_ = State::Idle;
  store_ = nullptr;
  headerLength_ = 0;
  expectedLength_ = 0;
  expectedCrc32_ = 0;
  runningCrc32_ = 0xFFFFFFFF;
  receivedLength_ = 0;
  payloadType_ = PayloadType::None;
  successTimeMs_ = 0;
  errorText_[0] = '\0';
}

const char *SensorProfileUpdater::statusText() const {
  switch (state_) {
    case State::Idle: return "Idle";
    case State::ReceivingHeader: return "Waiting for ESPCFG1/ESPCAL1";
    case State::ReceivingPayload:
      return payloadType_ == PayloadType::CalibrationPackage
                 ? "Receiving calibration"
                 : "Receiving JSON";
    case State::Success: return "Installed - restarting";
    case State::Failed: return "Upload failed";
  }
  return "Unknown";
}

bool SensorProfileUpdater::shouldRestart() const {
  return state_ == State::Success &&
         (uint32_t)(millis() - successTimeMs_) >= kRestartDelayMs;
}

void SensorProfileUpdater::handleHeader(Stream &usb) {
  while (usb.available() && headerLength_ < kHeaderLength) {
    header_[headerLength_++] = usb.read();
  }
  if (headerLength_ < kHeaderLength) return;

  if (memcmp(header_, kSensorMagic, kMagicLength) == 0) {
    payloadType_ = PayloadType::SensorProfiles;
  } else if (memcmp(header_, kCalibrationMagic, kMagicLength) == 0) {
    payloadType_ = PayloadType::CalibrationPackage;
  } else {
    fail(usb, "Bad magic");
    return;
  }
  expectedLength_ = readLe32(header_ + kMagicLength);
  expectedCrc32_ = readLe32(header_ + kMagicLength + 4);
  uint32_t maximumLength =
      payloadType_ == PayloadType::CalibrationPackage
          ? kMaxCalibrationPackageSize
          : SensorProfileStore::kMaxJsonFileSize;
  if (expectedLength_ == 0 || expectedLength_ > maximumLength) {
    fail(usb, "Length out of range");
    return;
  }

  const char *uploadPath =
      payloadType_ == PayloadType::CalibrationPackage
          ? kCalibrationPackagePath
          : kUploadingPath;
  uploadFile_ = LittleFS.open(uploadPath, "w");
  if (!uploadFile_) {
    fail(usb, "Cannot create temp file");
    return;
  }
  runningCrc32_ = 0xFFFFFFFF;
  receivedLength_ = 0;
  state_ = State::ReceivingPayload;
  // R = Ready。PC 收到后才允许发送第一个 64 字节数据块。
  usb.write((uint8_t)'R');
  usb.flush();
}

void SensorProfileUpdater::handlePayload(Stream &usb) {
  uint8_t chunk[kChunkSize];
  size_t chunkLength = 0;
  while (usb.available() && chunkLength < sizeof(chunk) &&
         receivedLength_ + chunkLength < expectedLength_) {
    int value = usb.read();
    if (value < 0) break;
    chunk[chunkLength++] = value;
  }
  if (chunkLength == 0) return;

  if (uploadFile_.write(chunk, chunkLength) != chunkLength) {
    fail(usb, "LittleFS write failed");
    return;
  }
  runningCrc32_ = updateCrc32(runningCrc32_, chunk, chunkLength);
  receivedLength_ += chunkLength;
  // P = Payload block accepted。PC 必须逐块等待，形成应用层流控制，
  // 避免 ESP32-S3 原生 USB CDC 的小接收缓冲区发生静默丢字节。
  usb.write((uint8_t)'P');
  usb.flush();
  if (receivedLength_ < expectedLength_) return;

  uploadFile_.flush();
  uploadFile_.close();
  uint32_t actualCrc = runningCrc32_ ^ 0xFFFFFFFF;
  if (actualCrc != expectedCrc32_) {
    fail(usb, "CRC32 mismatch");
    return;
  }
  if (payloadType_ == PayloadType::CalibrationPackage) {
    if (!installCalibrationPackage()) {
      char validationError[96] = "Calibration install failed";
      if (calibrationStore_ != nullptr &&
          calibrationStore_->lastError()[0] != '\0') {
        snprintf(validationError, sizeof(validationError), "%s",
                 calibrationStore_->lastError());
      }
      fail(usb, validationError);
      return;
    }
  } else {
    if (store_ == nullptr || !store_->activateFile(kUploadingPath)) {
      char validationError[96] = "Store unavailable";
      if (store_ != nullptr) {
        snprintf(validationError, sizeof(validationError), "%s",
                 store_->lastError());
        // Candidate parsing uses the same fixed runtime buffer as the active
        // configuration. Reload the untouched current file so a rejected upload
        // cannot change the running profile set before the next reboot.
        store_->begin(kCurrentPath);
      }
      fail(usb, validationError);
      return;
    }
    if (!installCandidate()) {
      // installCandidate() restores the old file name when replacement fails;
      // keep the in-memory store consistent with that restored file as well.
      store_->begin(kCurrentPath);
      fail(usb, "Atomic replace failed");
      return;
    }
  }

  usb.print("ACK\n");
  state_ = State::Success;
  successTimeMs_ = millis();
}

bool SensorProfileUpdater::installCandidate() {
  removeIfExists(kBackupPath);
  bool hadCurrent = LittleFS.exists(kCurrentPath);
  if (hadCurrent && !LittleFS.rename(kCurrentPath, kBackupPath)) return false;

  if (!LittleFS.rename(kUploadingPath, kCurrentPath)) {
    if (hadCurrent) LittleFS.rename(kBackupPath, kCurrentPath);
    return false;
  }
  return true;
}

bool SensorProfileUpdater::installCalibrationPackage() {
  if (calibrationStore_ == nullptr) return false;
  if (!LittleFS.exists("/calibration") && !LittleFS.mkdir("/calibration")) {
    return false;
  }
  if (!unpackCalibrationPackage()) return false;
  if (!calibrationStore_->validateBundle(kCalibrationIndexUploadingPath)) {
    return false;
  }
  if (!commitCalibrationIndex()) return false;
  removeIfExists(kCalibrationPackagePath);
  return true;
}

bool SensorProfileUpdater::unpackCalibrationPackage() {
  File package = LittleFS.open(kCalibrationPackagePath, "r");
  if (!package) return false;

  uint8_t packageHeader[kMagicLength + 2] = {0};
  if (!readExact(package, packageHeader, sizeof(packageHeader)) ||
      memcmp(packageHeader, kPackageMagic, kMagicLength) != 0) {
    package.close();
    return false;
  }
  uint16_t fileCount = static_cast<uint16_t>(packageHeader[kMagicLength]) |
                       (static_cast<uint16_t>(packageHeader[kMagicLength + 1]) << 8);
  if (fileCount < 2 ||
      fileCount > CalibrationProfileStore::kMaxProfiles + 1) {
    package.close();
    return false;
  }

  for (uint16_t fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
    uint8_t entryHeader[kPackageEntryHeaderLength] = {0};
    if (!readExact(package, entryHeader, sizeof(entryHeader))) {
      package.close();
      return false;
    }
    uint8_t kind = entryHeader[0];
    uint8_t pathLength = entryHeader[1];
    uint32_t length = readLe32(entryHeader + 2);
    uint32_t expectedCrc = readLe32(entryHeader + 6);
    bool indexEntry = kind == 2;
    if (pathLength == 0 || pathLength >= 80 ||
        (indexEntry != (fileIndex + 1 == fileCount)) ||
        (!indexEntry && kind != 1) ||
        (indexEntry && length > CalibrationProfileStore::kMaxIndexFileSize) ||
        (!indexEntry && length > CalibrationProfileStore::kMaxProfileFileSize) ||
        length == 0) {
      package.close();
      return false;
    }
    char path[80] = {0};
    if (!readExact(package, reinterpret_cast<uint8_t *>(path), pathLength)) {
      package.close();
      return false;
    }
    if ((indexEntry && strcmp(path, kCalibrationIndexPath) != 0) ||
        (!indexEntry && !validCalibrationProfilePath(path)) ||
        !copyPackageEntry(package, path, length, expectedCrc, indexEntry)) {
      package.close();
      return false;
    }
  }

  bool complete = package.position() == package.size();
  package.close();
  return complete;
}

bool SensorProfileUpdater::copyPackageEntry(File &package, const char *path,
                                            uint32_t length,
                                            uint32_t expectedCrc,
                                            bool indexEntry) {
  const char *temporaryPath = indexEntry
                                  ? kCalibrationIndexUploadingPath
                                  : kCalibrationProfileUploadingPath;
  removeIfExists(temporaryPath);
  File output = LittleFS.open(temporaryPath, "w");
  if (!output) return false;

  uint8_t buffer[kChunkSize];
  uint32_t remaining = length;
  uint32_t crc = 0xFFFFFFFF;
  while (remaining > 0) {
    size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (!readExact(package, buffer, count) || output.write(buffer, count) != count) {
      output.close();
      removeIfExists(temporaryPath);
      return false;
    }
    crc = updateCrc32(crc, buffer, count);
    remaining -= count;
  }
  output.flush();
  output.close();
  if ((crc ^ 0xFFFFFFFF) != expectedCrc) {
    removeIfExists(temporaryPath);
    return false;
  }
  if (indexEntry) return true;

  if (LittleFS.exists(path)) {
    File existing = LittleFS.open(path, "r");
    if (!existing || existing.size() != length) {
      if (existing) existing.close();
      removeIfExists(temporaryPath);
      return false;
    }
    uint32_t existingCrc = 0xFFFFFFFF;
    while (existing.available()) {
      size_t count = existing.read(buffer, sizeof(buffer));
      if (count == 0) break;
      existingCrc = updateCrc32(existingCrc, buffer, count);
    }
    existing.close();
    removeIfExists(temporaryPath);
    return (existingCrc ^ 0xFFFFFFFF) == expectedCrc;
  }
  if (!LittleFS.rename(temporaryPath, path)) {
    removeIfExists(temporaryPath);
    return false;
  }
  return true;
}

bool SensorProfileUpdater::commitCalibrationIndex() {
  removeIfExists(kCalibrationIndexBackupPath);
  bool hadCurrent = LittleFS.exists(kCalibrationIndexPath);
  if (hadCurrent &&
      !LittleFS.rename(kCalibrationIndexPath, kCalibrationIndexBackupPath)) {
    return false;
  }
  if (!LittleFS.rename(kCalibrationIndexUploadingPath, kCalibrationIndexPath)) {
    if (hadCurrent) {
      LittleFS.rename(kCalibrationIndexBackupPath, kCalibrationIndexPath);
    }
    return false;
  }
  return true;
}

bool SensorProfileUpdater::readExact(File &file, uint8_t *buffer,
                                     size_t length) {
  size_t offset = 0;
  while (offset < length) {
    size_t count = file.read(buffer + offset, length - offset);
    if (count == 0) return false;
    offset += count;
  }
  return true;
}

bool SensorProfileUpdater::validCalibrationProfilePath(const char *path) const {
  static constexpr char prefix[] = "/calibration/p";
  if (path == nullptr || strncmp(path, prefix, sizeof(prefix) - 1) != 0) {
    return false;
  }
  const char *cursor = path + sizeof(prefix) - 1;
  if (*cursor < '0' || *cursor > '9') return false;
  while (*cursor >= '0' && *cursor <= '9') ++cursor;
  if (*cursor++ != '-') return false;
  for (uint8_t index = 0; index < 8; ++index) {
    bool decimal = *cursor >= '0' && *cursor <= '9';
    bool lowerHex = *cursor >= 'a' && *cursor <= 'f';
    if (!decimal && !lowerHex) return false;
    ++cursor;
  }
  return strcmp(cursor, ".json") == 0;
}

void SensorProfileUpdater::fail(Stream &usb, const char *message) {
  if (uploadFile_) uploadFile_.close();
  removeIfExists(kUploadingPath);
  removeIfExists(kCalibrationPackagePath);
  removeIfExists(kCalibrationIndexUploadingPath);
  removeIfExists(kCalibrationProfileUploadingPath);
  snprintf(errorText_, sizeof(errorText_), "%s", message != nullptr ? message : "Unknown");
  usb.print("NAK:");
  usb.print(errorText_);
  usb.print('\n');
  state_ = State::Failed;
}

uint32_t SensorProfileUpdater::readLe32(const uint8_t *data) {
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

uint32_t SensorProfileUpdater::updateCrc32(uint32_t crc,
                                           const uint8_t *data,
                                           size_t length) {
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
    }
  }
  return crc;
}
