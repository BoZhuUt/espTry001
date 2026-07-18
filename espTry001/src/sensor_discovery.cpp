#include "sensor_discovery.h"

SensorDiscovery::SensorDiscovery(HardwareSerial &serial) : serial_(serial) {}

void SensorDiscovery::start(uint8_t maxAttempts, uint32_t timeoutMs) {
  // start() 同时清理上一次结果，保证重新扫描不会显示旧设备资料。
  maxAttempts_ = maxAttempts == 0 ? 1 : maxAttempts;
  timeoutMs_ = timeoutMs;
  attempt_ = 0;
  error_ = Error::None;
  exceptionCode_ = 0;
  result_ = {};
  sendAddressRequest();
}

void SensorDiscovery::cancel() {
  state_ = State::Idle;
  error_ = Error::None;
  responseLength_ = 0;
  expectedLength_ = 0;
}

void SensorDiscovery::sendAddressRequest() {
  sendReadRequest(0, kDiscoveryRegister, kDiscoveryQuantity,
                  State::DiscoveringAddress);
}

void SensorDiscovery::sendIdentityRequest() {
  sendReadRequest(result_.slaveAddress, kIdentityRegister,
                  kIdentityRegisterCount, State::ReadingIdentity);
}

void SensorDiscovery::sendReadRequest(uint8_t slaveAddress,
                                      uint16_t startAddress,
                                      uint16_t quantity,
                                      State waitingState) {
  // 丢弃上一事务残留数据，保证新响应从缓冲区第 0 字节开始。
  while (serial_.available()) serial_.read();

  uint8_t request[8] = {
    slaveAddress,
    0x03,
    (uint8_t)(startAddress >> 8),
    (uint8_t)(startAddress & 0xFF),
    (uint8_t)(quantity >> 8),
    (uint8_t)(quantity & 0xFF),
    0x00,
    0x00
  };
  uint16_t crc = crc16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = crc >> 8;

  responseLength_ = 0;
  expectedLength_ = 0;
  attempt_++;
  state_ = waitingState;
  requestStartTime_ = millis();
  serial_.write(request, sizeof(request));
  serial_.flush();
}

void SensorDiscovery::update() {
  // 这里只消费当前已经到达的字节，不等待串口，因此不会阻塞 UI。
  if (state_ != State::DiscoveringAddress &&
      state_ != State::ReadingIdentity) return;

  while (serial_.available()) {
    if (responseLength_ >= kResponseBufferSize) {
      retryOrFail(Error::BufferOverflow);
      return;
    }

    response_[responseLength_++] = serial_.read();
    if (responseLength_ >= 2 && (response_[1] & 0x80)) {
      expectedLength_ = 5;
    } else if (responseLength_ >= 3) {
      expectedLength_ = (size_t)response_[2] + 5;
      if (expectedLength_ > kResponseBufferSize) {
        retryOrFail(Error::InvalidLength);
        return;
      }
    }

    if (expectedLength_ > 0 && responseLength_ >= expectedLength_) {
      handleResponse();
      return;
    }
  }

  if ((unsigned long)(millis() - requestStartTime_) >= timeoutMs_) {
    retryOrFail(Error::Timeout);
  }
}

void SensorDiscovery::handleResponse() {
  if (responseLength_ != expectedLength_ || responseLength_ < 5) {
    retryOrFail(Error::InvalidLength);
    return;
  }

  uint16_t receivedCrc = response_[responseLength_ - 2] |
                         ((uint16_t)response_[responseLength_ - 1] << 8);
  if (crc16(response_, responseLength_ - 2) != receivedCrc) {
    retryOrFail(Error::CrcMismatch);
    return;
  }

  if (response_[1] & 0x80) {
    exceptionCode_ = response_[2];
    error_ = Error::ExceptionResponse;
    state_ = State::Failed;
    return;
  }

  if (response_[1] != 0x03) {
    retryOrFail(Error::InvalidFunction);
    return;
  }

  if (state_ == State::DiscoveringAddress) {
    handleAddressResponse();
  } else {
    handleIdentityResponse();
  }
}

void SensorDiscovery::handleAddressResponse() {
  // 本设备使用厂商自定义发现协议：请求地址为 0，响应地址为真实地址。
  if (response_[0] == 0 || response_[0] > 247) {
    retryOrFail(Error::InvalidAddress);
    return;
  }
  if (response_[2] != 2 || responseLength_ != 7) {
    retryOrFail(Error::InvalidLength);
    return;
  }

  result_.slaveAddress = response_[0];
  result_.runStatus = responseRegister(0);
  attempt_ = 0;
  error_ = Error::None;
  sendIdentityRequest();
}

void SensorDiscovery::handleIdentityResponse() {
  if (response_[0] != result_.slaveAddress) {
    retryOrFail(Error::InvalidAddress);
    return;
  }
  if (response_[2] != kIdentityRegisterCount * 2 ||
      responseLength_ != (size_t)(kIdentityRegisterCount * 2 + 5)) {
    retryOrFail(Error::InvalidLength);
    return;
  }

  result_.runStatus = responseRegister(0);
  result_.commStatus = responseRegister(1);
  result_.calibStatus = responseRegister(2);
  result_.configStatus = responseRegister(3);

  // PN 占两个寄存器，本项目设备采用低字寄存器在前（CDAB）。
  uint16_t productWord1 = responseRegister(4);
  uint16_t productWord2 = responseRegister(5);
  if (kProductLowWordFirst) {
    result_.productNum = ((uint32_t)productWord2 << 16) | productWord1;
  } else {
    result_.productNum = ((uint32_t)productWord1 << 16) | productWord2;
  }

  // 名称占 8 个寄存器；每个寄存器的低字节是前一个字符。
  size_t nameLength = 0;
  for (uint16_t i = 0; i < 8 && nameLength < 16; i++) {
    uint16_t word = responseRegister(6 + i);
    uint8_t chars[2];
    if (kNameHighByteFirst) {
      chars[0] = word >> 8;
      chars[1] = word & 0xFF;
    } else {
      chars[0] = word & 0xFF;
      chars[1] = word >> 8;
    }
    for (uint8_t c : chars) {
      if (c == 0 || c == 0xFF) {
        i = 8;
        break;
      }
      result_.deviceName[nameLength++] =
          (c >= 32 && c <= 126) ? (char)c : '?';
    }
  }
  while (nameLength > 0 && result_.deviceName[nameLength - 1] == ' ') {
    nameLength--;
  }
  result_.deviceName[nameLength] = '\0';

  error_ = Error::None;
  state_ = State::Success;
}

void SensorDiscovery::retryOrFail(Error error) {
  error_ = error;
  if (attempt_ < maxAttempts_) {
    if (state_ == State::ReadingIdentity) {
      sendIdentityRequest();
    } else {
      sendAddressRequest();
    }
  } else {
    state_ = State::Failed;
  }
}

uint16_t SensorDiscovery::responseRegister(uint16_t index) const {
  size_t offset = 3 + (size_t)index * 2;
  return ((uint16_t)response_[offset] << 8) | response_[offset + 1];
}

uint16_t SensorDiscovery::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
  }
  return crc;
}

const char *SensorDiscovery::errorText() const {
  switch (error_) {
    case Error::None: return "None";
    case Error::Timeout: return "Timeout";
    case Error::CrcMismatch: return "CRC Error";
    case Error::InvalidAddress: return "Invalid Address";
    case Error::InvalidFunction: return "Invalid Function";
    case Error::InvalidLength: return "Invalid Length";
    case Error::ExceptionResponse: return "Modbus Exception";
    case Error::BufferOverflow: return "Buffer Overflow";
  }
  return "Unknown";
}
