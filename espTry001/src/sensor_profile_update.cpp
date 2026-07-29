#include "sensor_profile_update.h"

#include <LittleFS.h>

constexpr char SensorProfileUpdater::kMagic[];
constexpr const char *SensorProfileUpdater::kCurrentPath;
constexpr const char *SensorProfileUpdater::kBackupPath;
constexpr const char *SensorProfileUpdater::kUploadingPath;

namespace {

void removeIfExists(const char *path) {
  if (LittleFS.exists(path)) LittleFS.remove(path);
}

}  // namespace

void SensorProfileUpdater::begin(SensorProfileStore &store) {
  cancel();
  store_ = &store;
  removeIfExists(kUploadingPath);
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
  state_ = State::Idle;
  store_ = nullptr;
  headerLength_ = 0;
  expectedLength_ = 0;
  expectedCrc32_ = 0;
  runningCrc32_ = 0xFFFFFFFF;
  receivedLength_ = 0;
  successTimeMs_ = 0;
  errorText_[0] = '\0';
}

const char *SensorProfileUpdater::statusText() const {
  switch (state_) {
    case State::Idle: return "Idle";
    case State::ReceivingHeader: return "Waiting for ESPCFG1";
    case State::ReceivingPayload: return "Receiving JSON";
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

  if (memcmp(header_, kMagic, kMagicLength) != 0) {
    fail(usb, "Bad magic");
    return;
  }
  expectedLength_ = readLe32(header_ + kMagicLength);
  expectedCrc32_ = readLe32(header_ + kMagicLength + 4);
  if (expectedLength_ == 0 ||
      expectedLength_ > SensorProfileStore::kMaxJsonFileSize) {
    fail(usb, "Length out of range");
    return;
  }

  uploadFile_ = LittleFS.open(kUploadingPath, "w");
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

void SensorProfileUpdater::fail(Stream &usb, const char *message) {
  if (uploadFile_) uploadFile_.close();
  removeIfExists(kUploadingPath);
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
