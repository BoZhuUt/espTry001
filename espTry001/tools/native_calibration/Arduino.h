#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

using std::size_t;

extern uint32_t gFakeMillis;
inline uint32_t millis() { return gFakeMillis; }

class HardwareSerial {
 public:
  int available() const { return static_cast<int>(rx_.size()); }
  int read() {
    if (rx_.empty()) return -1;
    uint8_t value = rx_.front();
    rx_.pop_front();
    return value;
  }
  size_t write(const uint8_t *data, size_t length) {
    tx_.insert(tx_.end(), data, data + length);
    return length;
  }
  void flush() {}

  void inject(const std::vector<uint8_t> &bytes) {
    rx_.insert(rx_.end(), bytes.begin(), bytes.end());
  }
  std::vector<uint8_t> takeTx() {
    std::vector<uint8_t> result = tx_;
    tx_.clear();
    return result;
  }

 private:
  std::deque<uint8_t> rx_;
  std::vector<uint8_t> tx_;
};
