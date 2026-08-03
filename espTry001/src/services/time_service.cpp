#include "services/time_service.h"

#include <esp_timer.h>

TimeService::TimeService(BoardRtc::Driver &driver) : driver_(driver) {}

void TimeService::begin() {
  if (driver_.begin()) {
    if (refreshFromRtc()) source_ = Source::Rtc;
  } else {
    valid_ = false;
    source_ = Source::None;
    status_ = mapDriverStatus(driver_.status());
  }
  lastRtcRefreshMs_ = millis();
}

void TimeService::update() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastRtcRefreshMs_) < kRtcRefreshIntervalMs) return;
  lastRtcRefreshMs_ = now;
  if (!valid_) {
    if (!driver_.begin()) {
      status_ = mapDriverStatus(driver_.status());
      return;
    }
  }
  refreshFromRtc();
}

bool TimeService::setUtcEpoch(int64_t utcSeconds, Source source) {
  const int64_t kEpoch2000 = 946684800LL;
  const int64_t kEpoch2100 = 4102444800LL;
  if (utcSeconds < kEpoch2000 || utcSeconds >= kEpoch2100) {
    status_ = Status::InvalidData;
    return false;
  }

  BoardRtc::DateTime value = dateTimeFromEpoch(utcSeconds);
  if (!driver_.setUtc(value)) {
    status_ = mapDriverStatus(driver_.status());
    return false;
  }
  setAnchor(utcSeconds);
  status_ = Status::Ready;
  source_ = source;
  valid_ = true;
  lastRtcRefreshMs_ = millis();
  return true;
}

const char *TimeService::statusText() const {
  switch (status_) {
    case Status::Unsupported:       return "UNSUPPORTED";
    case Status::NotPresent:        return "NOT PRESENT";
    case Status::NotSet:            return "NOT SET";
    case Status::OscillatorStopped: return "OSC STOP";
    case Status::InvalidData:       return "INVALID DATA";
    case Status::IoError:           return "I2C ERROR";
    case Status::Holdover:          return "HOLDOVER";
    case Status::Ready:             return "OK";
  }
  return "UNKNOWN";
}

const char *TimeService::sourceText() const {
  switch (source_) {
    case Source::None: return "NONE";
    case Source::Rtc:  return "RTC";
    case Source::Usb:  return "USB";
    case Source::Ntp:  return "NTP";
  }
  return "UNKNOWN";
}

int64_t TimeService::utcNow() const {
  if (!valid_) return 0;
  int64_t elapsedUs = esp_timer_get_time() - anchorMonotonicUs_;
  return anchorUtcSeconds_ + elapsedUs / 1000000LL;
}

void TimeService::formatLocalTime(char *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity == 0) return;
  if (!valid_) {
    snprintf(buffer, capacity, "--:--:--");
    return;
  }
  BoardRtc::DateTime local =
      dateTimeFromEpoch(utcNow() + kDisplayUtcOffsetSeconds);
  snprintf(buffer, capacity, "%02u:%02u:%02u",
           local.hour, local.minute, local.second);
}

void TimeService::formatLocalDateTime(char *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity == 0) return;
  if (!valid_) {
    buffer[0] = '\0';
    return;
  }
  BoardRtc::DateTime local =
      dateTimeFromEpoch(utcNow() + kDisplayUtcOffsetSeconds);
  snprintf(buffer, capacity, "%04u-%02u-%02u %02u:%02u:%02u",
           local.year, local.month, local.day,
           local.hour, local.minute, local.second);
}

void TimeService::formatIso8601(char *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity == 0) return;
  if (!valid_) {
    buffer[0] = '\0';
    return;
  }
  BoardRtc::DateTime local =
      dateTimeFromEpoch(utcNow() + kDisplayUtcOffsetSeconds);
  snprintf(buffer, capacity,
           "%04u-%02u-%02uT%02u:%02u:%02u+08:00",
           local.year, local.month, local.day,
           local.hour, local.minute, local.second);
}

bool TimeService::refreshFromRtc() {
  BoardRtc::DateTime value;
  if (!driver_.readUtc(value)) {
    Status failure = mapDriverStatus(driver_.status());
    if (valid_ && failure == Status::IoError) {
      status_ = Status::Holdover;
      return true;
    }
    valid_ = false;
    status_ = failure;
    return false;
  }

  int64_t rtcEpoch = epochFromDateTime(value);
  if (valid_) {
    int64_t current = utcNow();
    if (rtcEpoch + 1 < current) {
      status_ = Status::InvalidData;
      valid_ = false;
      return false;
    }
  }

  setAnchor(rtcEpoch);
  valid_ = true;
  status_ = Status::Ready;
  return true;
}

void TimeService::setAnchor(int64_t utcSeconds) {
  anchorUtcSeconds_ = utcSeconds;
  anchorMonotonicUs_ = esp_timer_get_time();
}

int64_t TimeService::epochFromDateTime(const BoardRtc::DateTime &value) {
  return daysFromCivil(value.year, value.month, value.day) * 86400LL +
         static_cast<int64_t>(value.hour) * 3600LL +
         static_cast<int64_t>(value.minute) * 60LL + value.second;
}

BoardRtc::DateTime TimeService::dateTimeFromEpoch(int64_t epochSeconds) {
  int64_t days = epochSeconds / 86400LL;
  int64_t secondsOfDay = epochSeconds % 86400LL;
  if (secondsOfDay < 0) {
    secondsOfDay += 86400LL;
    --days;
  }

  int year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  civilFromDays(days, year, month, day);

  BoardRtc::DateTime value;
  value.year = static_cast<uint16_t>(year);
  value.month = static_cast<uint8_t>(month);
  value.day = static_cast<uint8_t>(day);
  int64_t weekday = (days + 4) % 7;
  if (weekday < 0) weekday += 7;
  value.weekday = static_cast<uint8_t>(weekday);
  value.hour = static_cast<uint8_t>(secondsOfDay / 3600);
  value.minute = static_cast<uint8_t>((secondsOfDay % 3600) / 60);
  value.second = static_cast<uint8_t>(secondsOfDay % 60);
  return value;
}

int64_t TimeService::daysFromCivil(int year, unsigned month, unsigned day) {
  // 把 1/2 月视为上一年的第 11/12 月，使闰日在 400 年周期末统一处理。
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra =
      static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097LL +
         static_cast<int64_t>(dayOfEra) - 719468LL;
}

void TimeService::civilFromDays(int64_t days, int &year, unsigned &month,
                                unsigned &day) {
  // 719468 将 Unix epoch 平移到算法的 Gregorian 400 年周期原点。
  days += 719468LL;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned dayOfEra =
      static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
       dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const unsigned dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  month = monthPrime < 10 ? monthPrime + 3 : monthPrime - 9;
  year += month <= 2;
}

TimeService::Status TimeService::mapDriverStatus(BoardRtc::Status status) {
  switch (status) {
    case BoardRtc::Status::Unsupported:
      return Status::Unsupported;
    case BoardRtc::Status::NotPresent:
      return Status::NotPresent;
    case BoardRtc::Status::NotSet:
      return Status::NotSet;
    case BoardRtc::Status::OscillatorStopped:
      return Status::OscillatorStopped;
    case BoardRtc::Status::InvalidData:
      return Status::InvalidData;
    case BoardRtc::Status::IoError:
      return Status::IoError;
    case BoardRtc::Status::Ready:
      return Status::Ready;
  }
  return Status::IoError;
}
