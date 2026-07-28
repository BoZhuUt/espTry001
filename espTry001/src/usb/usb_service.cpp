#include "usb_service.h"

UsbService::UsbService(Stream &usb, HardwareSerial &rs485,
                       SensorProfileUpdater &profileUpdater,
                       StorageUsbBridge &logExportBridge,
                       uint8_t localSlaveId,
                       LocalFrameHandler localFrameHandler)
    : usb_(usb),
      rs485_(rs485),
      profileUpdater_(profileUpdater),
      logExportBridge_(logExportBridge),
      localSlaveId_(localSlaveId),
      localFrameHandler_(localFrameHandler) {}

void UsbService::update(AppMode mode) {
  // Exclusive application protocols get the complete USB byte stream. Do not
  // let Modbus framing inspect or buffer any part of their payload.
  if (mode == AppMode::ConfigUpload) {
    profileUpdater_.update(usb_);
    return;
  }
  if (mode == AppMode::LogExport) {
    // StorageTask owns ESPLOG1 file operations, but only loopTask touches the
    // physical CDC object. The fixed bridge preserves both ownership rules.
    logExportBridge_.pump(usb_);
    return;
  }
  if (mode == AppMode::Fault) return;

  bool passThrough = mode == AppMode::PassThrough;
  collectUsbBytes();

  if (rtuFrameLength_ > 0 &&
      (uint32_t)(micros() - lastUsbByteTimeUs_) >= kRtuFrameGapUs) {
    dispatchCompletedFrame(passThrough);
  }

  // Preserve the original order: when both directions are active in one loop,
  // the later RS485 reply is the transfer direction shown to the operator.
  forwardRs485ToUsb(passThrough);
}

void UsbService::clearRtuFrame() {
  rtuFrameLength_ = 0;
  lastUsbByteTimeUs_ = 0;
  activity_ = UsbTransferActivity::None;
}

void UsbService::clearLogExportTransport() {
  logExportBridge_.reset();
}

UsbTransferActivity UsbService::takeTransferActivity() {
  UsbTransferActivity result = activity_;
  activity_ = UsbTransferActivity::None;
  return result;
}

void UsbService::collectUsbBytes() {
  while (usb_.available()) {
    uint8_t value = (uint8_t)usb_.read();
    if (rtuFrameLength_ < kMaxRtuFrameLength) {
      rtuFrame_[rtuFrameLength_++] = value;
    } else {
      // Match the old gateway behavior: discard an oversized RTU frame and
      // use the next silence gap as a clean frame boundary.
      rtuFrameLength_ = 0;
    }
    lastUsbByteTimeUs_ = micros();
  }
}

void UsbService::dispatchCompletedFrame(bool passThrough) {
  if (rtuFrame_[0] == localSlaveId_) {
    if (localFrameHandler_ != nullptr) {
      localFrameHandler_(rtuFrame_, rtuFrameLength_);
    }
  } else if (passThrough) {
    rs485_.write(rtuFrame_, rtuFrameLength_);
    rs485_.flush();
    activity_ = UsbTransferActivity::UsbToRs485;
  }
  rtuFrameLength_ = 0;
}

void UsbService::forwardRs485ToUsb(bool passThrough) {
  if (!passThrough || !rs485_.available()) return;

  while (rs485_.available()) {
    usb_.write((uint8_t)rs485_.read());
  }
  activity_ = UsbTransferActivity::Rs485ToUsb;
}
