#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

// Fixed-memory, task-safe byte bridge between StorageTask's ESPLOG1 state
// machine and UsbService in Arduino loopTask. StorageTask sees a Stream, while
// only UsbService touches the physical USB CDC object.
class StorageUsbBridge : public Stream {
 public:
  static constexpr size_t kRxCapacity = 512;
  static constexpr size_t kTxCapacity = 8192;
  static constexpr size_t kUsbPumpChunk = 64;

  bool begin();
  void reset();

  // Called only by UsbService/loopTask.
  void pump(Stream &physicalUsb);

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;

 private:
  StaticStreamBuffer_t rxControl_{};
  StaticStreamBuffer_t txControl_{};
  uint8_t rxStorage_[kRxCapacity + 1] = {0};
  uint8_t txStorage_[kTxCapacity + 1] = {0};
  StreamBufferHandle_t rxBuffer_ = nullptr;
  StreamBufferHandle_t txBuffer_ = nullptr;

  int peekedByte_ = -1;
  uint8_t pendingTx_[kUsbPumpChunk] = {0};
  size_t pendingTxLength_ = 0;
  size_t pendingTxOffset_ = 0;
};
