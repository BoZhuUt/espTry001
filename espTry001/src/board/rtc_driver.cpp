#include "board/rtc_driver.h"

#include <Wire.h>

#include "board/board_config.h"

namespace BoardRtc {

bool Driver::begin() {
  if (!BoardConfig::kHasRtc) {
    status_ = Status::Unsupported;
    return false;
  }

  if (!Wire1.begin(BoardConfig::kRtcSda, BoardConfig::kRtcScl, 100000)) {
    status_ = Status::IoError;
    return false;
  }
  initialized_ = true;

  if (!probe()) {
    status_ = Status::NotPresent;
    return false;
  }

  uint8_t control1 = 0;
  if (!readRegisters(kControl1Register, &control1, 1)) {
    status_ = Status::IoError;
    return false;
  }

  bool wasStopped = (control1 & kControl1Stop) != 0;
  if (control1 != kControl1Normal) {
    const uint8_t normal = kControl1Normal;
    if (!writeRegisters(kControl1Register, &normal, 1)) {
      status_ = Status::IoError;
      return false;
    }
  }
  if (wasStopped) {
    status_ = Status::OscillatorStopped;
    return false;
  }

  DateTime ignored;
  return readUtc(ignored);
}

bool Driver::readUtc(DateTime &out) {
  if (!initialized_) {
    status_ = BoardConfig::kHasRtc ? Status::IoError : Status::Unsupported;
    return false;
  }

  uint8_t registers[7] = {0};
  if (!readRegisters(kSecondsRegister, registers, sizeof(registers))) {
    status_ = Status::IoError;
    return false;
  }
  if ((registers[0] & kSecondsOscillatorStop) != 0) {
    status_ = Status::OscillatorStopped;
    return false;
  }

  DateTime decoded;
  uint8_t year = 0;
  if (!decodeBcd(registers[0], 0x7F, decoded.second) ||
      !decodeBcd(registers[1], 0x7F, decoded.minute) ||
      !decodeBcd(registers[2], 0x3F, decoded.hour) ||
      !decodeBcd(registers[3], 0x3F, decoded.day) ||
      !decodeBcd(registers[4], 0x07, decoded.weekday) ||
      !decodeBcd(registers[5], 0x1F, decoded.month) ||
      !decodeBcd(registers[6], 0xFF, year)) {
    status_ = Status::InvalidData;
    return false;
  }
  decoded.year = static_cast<uint16_t>(2000 + year);
  if (!isValidDateTime(decoded)) {
    status_ = Status::InvalidData;
    return false;
  }

  out = decoded;
  status_ = Status::Ready;
  return true;
}

bool Driver::setUtc(const DateTime &value) {
  if (!initialized_ || !isValidDateTime(value)) {
    status_ = initialized_ ? Status::InvalidData : Status::IoError;
    return false;
  }

  const uint8_t normal = kControl1Normal;
  if (!writeRegisters(kControl1Register, &normal, 1)) {
    status_ = Status::IoError;
    return false;
  }

  uint8_t registers[7] = {
      encodeBcd(value.second),
      encodeBcd(value.minute),
      encodeBcd(value.hour),
      encodeBcd(value.day),
      encodeBcd(value.weekday),
      encodeBcd(value.month),
      encodeBcd(static_cast<uint8_t>(value.year - 2000)),
  };
  if (!writeRegisters(kSecondsRegister, registers, sizeof(registers))) {
    status_ = Status::IoError;
    return false;
  }

  DateTime verified;
  if (!readUtc(verified)) return false;
  // 回读可能恰好跨过分钟边界，先确认日期和时分，再允许秒数前进 1 秒。
  const int secondAdvance =
      (static_cast<int>(verified.second) -
       static_cast<int>(value.second) + 60) % 60;
  if (verified.year != value.year || verified.month != value.month ||
      verified.day != value.day || verified.hour != value.hour ||
      (verified.minute != value.minute &&
       !(value.second == 59 && verified.second == 0)) ||
      secondAdvance > 1) {
    status_ = Status::InvalidData;
    return false;
  }
  status_ = Status::Ready;
  return true;
}

const char *Driver::statusText() const {
  switch (status_) {
    case Status::Unsupported:       return "UNSUPPORTED";
    case Status::NotPresent:        return "NOT PRESENT";
    case Status::NotSet:            return "NOT SET";
    case Status::OscillatorStopped: return "OSC STOP";
    case Status::InvalidData:       return "INVALID DATA";
    case Status::IoError:           return "I2C ERROR";
    case Status::Ready:             return "OK";
  }
  return "UNKNOWN";
}

bool Driver::probe() {
  Wire1.beginTransmission(BoardConfig::kRtcI2cAddress);
  return Wire1.endTransmission() == 0;
}

bool Driver::readRegisters(uint8_t firstRegister, uint8_t *data,
                           size_t length) {
  Wire1.beginTransmission(BoardConfig::kRtcI2cAddress);
  Wire1.write(firstRegister);
  if (Wire1.endTransmission(false) != 0) return false;

  size_t received = Wire1.requestFrom(
      static_cast<uint8_t>(BoardConfig::kRtcI2cAddress),
      static_cast<uint8_t>(length));
  if (received != length) {
    while (Wire1.available()) Wire1.read();
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    data[index] = static_cast<uint8_t>(Wire1.read());
  }
  return true;
}

bool Driver::writeRegisters(uint8_t firstRegister, const uint8_t *data,
                            size_t length) {
  Wire1.beginTransmission(BoardConfig::kRtcI2cAddress);
  Wire1.write(firstRegister);
  if (Wire1.write(data, length) != length) {
    Wire1.endTransmission();
    return false;
  }
  return Wire1.endTransmission() == 0;
}

bool Driver::decodeBcd(uint8_t encoded, uint8_t mask, uint8_t &decoded) {
  encoded &= mask;
  uint8_t high = encoded >> 4;
  uint8_t low = encoded & 0x0F;
  if (high > 9 || low > 9) return false;
  decoded = static_cast<uint8_t>(high * 10 + low);
  return true;
}

uint8_t Driver::encodeBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool Driver::isValidDateTime(const DateTime &value) {
  return value.year >= 2000 && value.year <= 2099 &&
         value.month >= 1 && value.month <= 12 &&
         value.day >= 1 &&
         value.day <= daysInMonth(value.year, value.month) &&
         value.weekday <= 6 && value.hour <= 23 &&
         value.minute <= 59 && value.second <= 59;
}

uint8_t Driver::daysInMonth(uint16_t year, uint8_t month) {
  static constexpr uint8_t kDays[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 0 || month > 12) return 0;
  if (month != 2) return kDays[month - 1];
  bool leap = (year % 4 == 0) &&
              ((year % 100 != 0) || (year % 400 == 0));
  return leap ? 29 : 28;
}

}  // namespace BoardRtc
