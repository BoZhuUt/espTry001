#include "data_logger.h"
#include "debug_log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <stdarg.h>

DataLogger::DataLogger(SdStorage &sd) : sd_(sd) {}

void DataLogger::setMetadata(const char *configVersion,
                             const char *firmwareVersion) {
  snprintf(configVersion_, sizeof(configVersion_), "%s",
           configVersion != nullptr ? configVersion : "unknown");
  snprintf(firmwareVersion_, sizeof(firmwareVersion_), "%s",
           firmwareVersion != nullptr ? firmwareVersion : "unknown");
}

void DataLogger::loadSessionId() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_u32(handle, kNvsSessionKey, &sessionId_);
    nvs_close(handle);
  }
  // 默认从 0 开始，创建文件时递增
}

void DataLogger::incrementSessionId() {
  sessionId_++;
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u32(handle, kNvsSessionKey, sessionId_);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void DataLogger::setIdentity(uint8_t slaveAddress, uint32_t productNum,
                              const char *profileName,
                              const SensorProfile *profile) {
  slaveAddress_ = slaveAddress;
  productNum_ = productNum;
  snprintf(profileName_, sizeof(profileName_), "%s",
           profileName != nullptr ? profileName : "?");
  profile_ = profile;
}

void DataLogger::start() {
  if (recording_) return;
  if (sd_.state() != SdStorage::State::Ready) {
    snprintf(lastError_, sizeof(lastError_), "SD not ready");
    return;
  }
  if (profile_ == nullptr || profile_->fieldCount > kMaxSensorFields) {
    snprintf(lastError_, sizeof(lastError_), "Invalid sensor profile");
    return;
  }

  loadSessionId();
  incrementSessionId();
  lastLoggedSequence_ = 0;
  sampleSeq_ = 0;
  recordCount_ = 0;
  fileVolume_ = 0;
  ringHead_ = 0;
  ringTail_ = 0;
  ringUsed_ = 0;
  lastFlushMs_ = millis();
  lastSuccessfulFlushMs_ = 0;

  if (!createFile()) return;

  recording_ = true;
  DEBUG_LOG_PRINTF("[Log] Recording started: %s, session=%lu\n",
                   fileName_, (unsigned long)sessionId_);
}

void DataLogger::stop() {
  if (!recording_ && !fileOpen_) return;

  // 把缓冲区剩余数据全部写入
  if (fileOpen_) {
    flushToFile();
    closeFile();
  }

  recording_ = false;
  DEBUG_LOG_PRINTF("[Log] Stopped, %lu records saved\n",
                   (unsigned long)recordCount_);
}

bool DataLogger::createFile() {
  if (sd_.state() != SdStorage::State::Ready) return false;

  if (!sd_.ensureDir("/logs")) return false;

  // 文件名：PN53607_S000012.csv
  if (fileVolume_ == 0) {
    snprintf(fileName_, sizeof(fileName_), "/logs/PN%lu_S%06lu.csv",
             (unsigned long)productNum_, (unsigned long)sessionId_);
  } else {
    snprintf(fileName_, sizeof(fileName_), "/logs/PN%lu_S%06lu_v%lu.csv",
             (unsigned long)productNum_, (unsigned long)sessionId_,
             (unsigned long)fileVolume_ + 1);
  }

  file_ = sd_.open(fileName_, FILE_WRITE);
  if (!file_) {
    snprintf(lastError_, sizeof(lastError_), "Create file failed");
    sd_.setError("Create file failed");
    return false;
  }

  fileOpen_ = true;
  headerWritten_ = false;

  if (!writeFileHeader()) {
    closeFile();
    return false;
  }

  return true;
}

bool DataLogger::writeFileHeader() {
  if (!fileOpen_ || profile_ == nullptr) return false;

  char line[kLineBufferSize];
  size_t len = 0;
  if (!appendFormat(line, sizeof(line), len,
                    "# config_version=%s\n# session_id=%lu\n"
                    "# firmware=%s\n# time_source=uptime_ms\n",
                    configVersion_[0] ? configVersion_ : "unknown",
                    (unsigned long)sessionId_,
                    firmwareVersion_[0] ? firmwareVersion_ : "unknown")) {
    snprintf(lastError_, sizeof(lastError_), "CSV header too long");
    return false;
  }
  if (file_.write((const uint8_t *)line, len) != len) {
    sd_.setError("Header write failed");
    return false;
  }

  len = 0;
  if (!appendFormat(line, sizeof(line), len,
                    "session_id,sample_seq,uptime_ms,address,pn,CH1_mA,CH2_mA")) {
    return false;
  }

  for (uint8_t i = 0; i < profile_->fieldCount; i++) {
    if (!appendFormat(line, sizeof(line), len, ",%s",
                      profile_->fields[i].label)) {
      snprintf(lastError_, sizeof(lastError_), "CSV header too long");
      return false;
    }
  }
  if (!appendFormat(line, sizeof(line), len, "\n")) return false;

  if (file_.write((const uint8_t *)line, len) != len) {
    sd_.setError("Header write failed");
    return false;
  }

  file_.flush();
  headerWritten_ = true;
  return true;
}

void DataLogger::onScanComplete(uint32_t scanSequence,
                                const float *values, const bool *valid,
                                uint8_t fieldCount,
                                float current1Ma, float current2Ma) {
  if (!recording_) return;

  // 只在扫描序号变化时记录，避免同一轮重复写入
  if (scanSequence == lastLoggedSequence_) return;
  if (formatCsvLine(values, valid, fieldCount, current1Ma, current2Ma)) {
    lastLoggedSequence_ = scanSequence;
    sampleSeq_++;
    recordCount_++;
  }
}

bool DataLogger::formatCsvLine(const float *values, const bool *valid,
                                uint8_t fieldCount,
                                float current1Ma, float current2Ma) {
  char line[kLineBufferSize];
  size_t len = 0;

  if (!appendFormat(line, sizeof(line), len,
                    "%lu,%lu,%lu,%u,%lu,%.2f,%.2f",
                    (unsigned long)sessionId_,
                    (unsigned long)(sampleSeq_ + 1),
                    (unsigned long)millis(), slaveAddress_,
                    (unsigned long)productNum_, current1Ma, current2Ma)) {
    snprintf(lastError_, sizeof(lastError_), "CSV row too long");
    recording_ = false;
    return false;
  }

  // Profile 参数列
  for (uint8_t i = 0; i < fieldCount && i < kMaxSensorFields; i++) {
    if (valid[i]) {
      uint8_t decimals = 0;
      if (profile_ != nullptr && i < profile_->fieldCount) {
        decimals = profile_->fields[i].decimals;
      }
      // 动态精度格式化
      char fmt[8];
      snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);
      if (!appendFormat(line, sizeof(line), len, ",") ||
          !appendFormat(line, sizeof(line), len, fmt, values[i])) {
        snprintf(lastError_, sizeof(lastError_), "CSV row too long");
        recording_ = false;
        return false;
      }
    } else {
      if (!appendFormat(line, sizeof(line), len, ",")) return false;
    }
  }

  if (!appendFormat(line, sizeof(line), len, "\n")) return false;
  return ringWrite(line, len);
}

bool DataLogger::appendFormat(char *buffer, size_t capacity, size_t &length,
                              const char *format, ...) {
  if (length >= capacity) return false;
  va_list args;
  va_start(args, format);
  int result = vsnprintf(buffer + length, capacity - length, format, args);
  va_end(args);
  if (result < 0 || (size_t)result >= capacity - length) return false;
  length += (size_t)result;
  return true;
}

bool DataLogger::ringWrite(const char *data, size_t length) {
  if (length > kRingBufferSize) {
    snprintf(lastError_, sizeof(lastError_), "CSV row exceeds buffer");
    recording_ = false;
    return false;
  }
  if (length > kRingBufferSize - ringUsed_ && !flushToFile()) return false;
  if (length > kRingBufferSize - ringUsed_) return false;

  for (size_t i = 0; i < length; i++) {
    ring_[ringHead_] = data[i];
    ringHead_ = (ringHead_ + 1) % kRingBufferSize;
    ringUsed_++;
  }
  return true;
}

bool DataLogger::flushToFile() {
  if (!fileOpen_ || ringUsed_ == 0) return true;

  // 从环形缓冲区读取数据并写入文件
  size_t toWrite = ringUsed_;
  size_t written = 0;
  char chunk[256];

  while (written < toWrite) {
    size_t chunkLen = toWrite - written;
    if (chunkLen > sizeof(chunk)) chunkLen = sizeof(chunk);

    for (size_t i = 0; i < chunkLen; i++) {
      chunk[i] = ring_[ringTail_];
      ringTail_ = (ringTail_ + 1) % kRingBufferSize;
    }

    size_t result = file_.write((const uint8_t *)chunk, chunkLen);
    if (result != chunkLen) {
      sd_.setError("SD write failed");
      recording_ = false;
      closeFile();
      return false;
    }
    written += chunkLen;
  }

  ringUsed_ = 0;
  ringHead_ = 0;
  ringTail_ = 0;
  lastSuccessfulFlushMs_ = millis();

  // 检查文件大小是否需要分卷
  if (file_.size() >= (ssize_t)kMaxFileSize) {
    closeFile();
    fileVolume_++;
    if (!createFile()) return false;
  }

  return true;
}

void DataLogger::closeFile() {
  if (fileOpen_) {
    file_.flush();
    file_.close();
    fileOpen_ = false;
  }
}

void DataLogger::update() {
  if (!recording_) return;

  // 缓冲区超过一半时立即写入
  bool shouldFlush = (ringUsed_ >= kRingBufferSize / 2);

  // 定期 flush
  uint32_t now = millis();
  if (!shouldFlush && ringUsed_ > 0 &&
      (uint32_t)(now - lastFlushMs_) >= kFullFlushIntervalMs) {
    shouldFlush = true;
  }

  if (shouldFlush) {
    flushToFile();
    lastFlushMs_ = now;

    // 每次 flush 后也做一次 file flush，确保数据落盘
    if (fileOpen_) {
      file_.flush();
    }
  }
}
