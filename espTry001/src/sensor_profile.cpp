#include "sensor_profile.h"

namespace {

// JSON 是 Profile 的唯一人工维护入口；此文件由编译前脚本生成。
#include "sensor_profiles_generated.inc"

// 将设备连续返回的两个寄存器按 CDAB 字顺序还原为 IEEE-754 float。
float decodeFloatCDAB(uint16_t firstWord, uint16_t secondWord) {
  uint32_t raw = ((uint32_t)secondWord << 16) | firstWord;
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

} // namespace

const SensorProfile *findSensorProfile(uint32_t productNum) {
  for (const SensorProfile &profile : kProfiles) {
    if (profile.productNum == productNum) return &profile;
  }
  return nullptr;
}

const char *builtInProfileConfigVersion() {
  return kBuiltInProfileConfigVersion;
}

SensorProfilePoller::SensorProfilePoller(HardwareSerial &serial)
    : serial_(serial) {}

void SensorProfilePoller::begin(uint8_t slaveAddress,
                                const SensorProfile &profile,
                                float *values, bool *valid) {
  slaveAddress_ = slaveAddress;
  profile_ = &profile;
  values_ = values;
  valid_ = valid;
  blockIndex_ = 0;
  responseLength_ = 0;
  expectedLength_ = 0;
  waiting_ = false;
  scanCompleted_ = false;
  nextRequestMs_ = millis();
}

void SensorProfilePoller::cancel() {
  profile_ = nullptr;
  waiting_ = false;
  responseLength_ = 0;
  expectedLength_ = 0;
  scanCompleted_ = false;
}

bool SensorProfilePoller::takeScanCompleted() {
  if (scanCompleted_) {
    scanCompleted_ = false;
    return true;
  }
  return false;
}

bool SensorProfilePoller::update(uint8_t &lastError) {
  if (profile_ == nullptr || profile_->blockCount == 0) return false;

  if (!waiting_) {
    if ((int32_t)(millis() - nextRequestMs_) >= 0) sendCurrentBlock();
    return false;
  }

  while (serial_.available()) {
    if (responseLength_ >= kResponseBufferSize) {
      finishCurrentBlock(ModbusMaster::ku8MBInvalidCRC);
      lastError = ModbusMaster::ku8MBInvalidCRC;
      return true;
    }

    response_[responseLength_++] = serial_.read();
    if (responseLength_ >= 2 && (response_[1] & 0x80)) {
      expectedLength_ = 5;
    } else if (responseLength_ >= 3) {
      expectedLength_ = (size_t)response_[2] + 5;
      if (expectedLength_ > kResponseBufferSize) {
        finishCurrentBlock(ModbusMaster::ku8MBInvalidCRC);
        lastError = ModbusMaster::ku8MBInvalidCRC;
        return true;
      }
    }

    if (expectedLength_ > 0 && responseLength_ >= expectedLength_) {
      uint8_t error = ModbusMaster::ku8MBSuccess;
      uint16_t receivedCrc = response_[responseLength_ - 2] |
                             ((uint16_t)response_[responseLength_ - 1] << 8);
      const SensorPollBlock &block = profile_->blocks[blockIndex_];
      if (crc16(response_, responseLength_ - 2) != receivedCrc) {
        error = ModbusMaster::ku8MBInvalidCRC;
      } else if (response_[0] != slaveAddress_) {
        error = ModbusMaster::ku8MBInvalidSlaveID;
      } else if (response_[1] & 0x80) {
        error = response_[2];
      } else if (response_[1] != 0x03 ||
                 response_[2] != block.registerCount * 2) {
        error = ModbusMaster::ku8MBInvalidFunction;
      } else {
        decodeCurrentBlock();
      }
      finishCurrentBlock(error);
      lastError = error;
      return true;
    }
  }

  if ((uint32_t)(millis() - requestStartMs_) >= kResponseTimeoutMs) {
    finishCurrentBlock(ModbusMaster::ku8MBResponseTimedOut);
    lastError = ModbusMaster::ku8MBResponseTimedOut;
    return true;
  }
  return false;
}

void SensorProfilePoller::sendCurrentBlock() {
  // 清掉上一事务可能残留的字节，避免被误认为当前响应的开头。
  while (serial_.available()) serial_.read();

  const SensorPollBlock &block = profile_->blocks[blockIndex_];
  uint16_t protocolAddress = block.address1Based - 1;
  uint8_t request[8] = {
    slaveAddress_, 0x03,
    (uint8_t)(protocolAddress >> 8), (uint8_t)protocolAddress,
    (uint8_t)(block.registerCount >> 8), (uint8_t)block.registerCount,
    0, 0
  };
  uint16_t crc = crc16(request, 6);
  request[6] = crc;
  request[7] = crc >> 8;

  responseLength_ = 0;
  expectedLength_ = 0;
  waiting_ = true;
  requestStartMs_ = millis();
  serial_.write(request, sizeof(request));
  serial_.flush();
}

void SensorProfilePoller::finishCurrentBlock(uint8_t error) {
  // 只把当前读取块所属字段标为无效，其他块的最近有效值仍保留。
  for (uint8_t fieldIndex = 0; fieldIndex < profile_->fieldCount; fieldIndex++) {
    if (profile_->fields[fieldIndex].blockIndex == blockIndex_ &&
        error != ModbusMaster::ku8MBSuccess) {
      valid_[fieldIndex] = false;
    }
  }

  waiting_ = false;
  responseLength_ = 0;
  expectedLength_ = 0;
  blockIndex_++;
  if (blockIndex_ >= profile_->blockCount) {
    blockIndex_ = 0;
    scanSequence_++;
    scanCompleted_ = true;
    nextRequestMs_ = millis() + kScanIntervalMs;
  } else {
    nextRequestMs_ = millis() + kInterBlockDelayMs;
  }
}

void SensorProfilePoller::decodeCurrentBlock() {
  for (uint8_t fieldIndex = 0; fieldIndex < profile_->fieldCount; fieldIndex++) {
    const SensorField &field = profile_->fields[fieldIndex];
    if (field.blockIndex != blockIndex_) continue;

    uint16_t firstWord = responseRegister(field.registerOffset);
    if (field.type == SensorValueType::UInt16) {
      values_[fieldIndex] = firstWord;
    } else {
      values_[fieldIndex] = decodeFloatCDAB(
          firstWord, responseRegister(field.registerOffset + 1));
    }
    valid_[fieldIndex] = true;
  }
}

uint16_t SensorProfilePoller::responseRegister(uint8_t offset) const {
  size_t byteOffset = 3 + (size_t)offset * 2;
  return ((uint16_t)response_[byteOffset] << 8) | response_[byteOffset + 1];
}

uint16_t SensorProfilePoller::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
  }
  return crc;
}
