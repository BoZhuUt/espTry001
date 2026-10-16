#pragma once

#include <Arduino.h>

// One loopTask owns all application TX. Keep Arduino CDC RX/events, but use
// TinyUSB endpoint readiness for TX so existing hosts need not assert DTR.
class UsbCdcStream : public Stream {
 public:
  explicit UsbCdcStream(Stream &rx) : rx_(rx) {}
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  int availableForWrite() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *data, size_t length) override;
  void flush() override;

 private:
  static constexpr uint32_t kWriteTimeoutMs = 250;
  Stream &rx_;
};
