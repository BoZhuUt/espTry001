#include "usb/storage_usb_bridge.h"

bool StorageUsbBridge::begin() {
  if (rxBuffer_ != nullptr && txBuffer_ != nullptr) return true;

  rxBuffer_ = xStreamBufferCreateStatic(
      kRxCapacity, 1, rxStorage_, &rxControl_);
  txBuffer_ = xStreamBufferCreateStatic(
      kTxCapacity, 1, txStorage_, &txControl_);
  return rxBuffer_ != nullptr && txBuffer_ != nullptr;
}

void StorageUsbBridge::reset() {
  if (rxBuffer_ != nullptr) xStreamBufferReset(rxBuffer_);
  if (txBuffer_ != nullptr) xStreamBufferReset(txBuffer_);
  peekedByte_ = -1;
  pendingTxLength_ = 0;
  pendingTxOffset_ = 0;
}

void StorageUsbBridge::pump(Stream &physicalUsb) {
  if (rxBuffer_ == nullptr || txBuffer_ == nullptr) return;

  // Feed PC commands to StorageTask without consuming more bytes than the
  // fixed RX bridge can accept.
  size_t rxSpace = xStreamBufferSpacesAvailable(rxBuffer_);
  while (rxSpace > 0 && physicalUsb.available()) {
    int value = physicalUsb.read();
    if (value < 0) break;
    uint8_t byteValue = static_cast<uint8_t>(value);
    if (xStreamBufferSend(rxBuffer_, &byteValue, 1, 0) != 1) break;
    rxSpace--;
  }

  // Preserve a partially accepted physical USB write across loop iterations.
  // Bytes leave the TX bridge only after the CDC driver reports acceptance.
  if (pendingTxOffset_ >= pendingTxLength_) {
    pendingTxLength_ =
        xStreamBufferReceive(txBuffer_, pendingTx_, sizeof(pendingTx_), 0);
    pendingTxOffset_ = 0;
  }
  if (pendingTxOffset_ < pendingTxLength_) {
    size_t accepted = physicalUsb.write(
        pendingTx_ + pendingTxOffset_, pendingTxLength_ - pendingTxOffset_);
    if (accepted > 0) {
      pendingTxOffset_ += accepted;
      physicalUsb.flush();
    }
  }
}

int StorageUsbBridge::available() {
  if (rxBuffer_ == nullptr) return 0;
  size_t count = xStreamBufferBytesAvailable(rxBuffer_);
  if (peekedByte_ >= 0) count++;
  return count > static_cast<size_t>(INT_MAX)
             ? INT_MAX
             : static_cast<int>(count);
}

int StorageUsbBridge::read() {
  if (peekedByte_ >= 0) {
    int value = peekedByte_;
    peekedByte_ = -1;
    return value;
  }
  if (rxBuffer_ == nullptr) return -1;
  uint8_t value = 0;
  return xStreamBufferReceive(rxBuffer_, &value, 1, 0) == 1 ? value : -1;
}

int StorageUsbBridge::peek() {
  if (peekedByte_ >= 0) return peekedByte_;
  peekedByte_ = read();
  return peekedByte_;
}

void StorageUsbBridge::flush() {
  // Acceptance into the fixed TX bridge is the synchronization boundary.
  // UsbService drains it from loopTask without dropping partial CDC writes.
}

size_t StorageUsbBridge::write(uint8_t value) {
  return write(&value, 1);
}

size_t StorageUsbBridge::write(const uint8_t *buffer, size_t size) {
  if (txBuffer_ == nullptr || buffer == nullptr || size == 0) return 0;
  return xStreamBufferSend(txBuffer_, buffer, size, 0);
}
