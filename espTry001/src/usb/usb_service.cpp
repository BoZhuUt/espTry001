#include "usb_service.h"

UsbService::UsbService(Stream &usb, Rs485Service &rs485Service,
                       SensorProfileUpdater &profileUpdater,
                       StorageUsbBridge &logExportBridge,
                       TimeSyncServer &timeSyncServer,
                       uint8_t localSlaveId,
                       LocalFrameHandler localFrameHandler)
    : usb_(usb),
      rs485Service_(rs485Service),
      profileUpdater_(profileUpdater),
      logExportBridge_(logExportBridge),
      timeSyncServer_(timeSyncServer),
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
  if (mode == AppMode::TimeSync) {
    timeSyncServer_.update(usb_);
    return;
  }
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
  passResponseLength_ = 0;
  passResponseOffset_ = 0;
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
    if (rs485Service_.enqueuePassFrame(rtuFrame_, rtuFrameLength_)) {
      activity_ = UsbTransferActivity::UsbToRs485;
    }
  }
  rtuFrameLength_ = 0;
}

void UsbService::forwardRs485ToUsb(bool passThrough) {
  if (!passThrough) {
    passResponseLength_ = 0;
    passResponseOffset_ = 0;
    return;
  }

  if (passResponseOffset_ >= passResponseLength_) {
    passResponseLength_ = rs485Service_.readPassResponse(
        passResponse_, sizeof(passResponse_));
    passResponseOffset_ = 0;
  }
  if (passResponseOffset_ < passResponseLength_) {
    size_t accepted = usb_.write(
        passResponse_ + passResponseOffset_,
        passResponseLength_ - passResponseOffset_);
    if (accepted > 0) {
      passResponseOffset_ += accepted;
      usb_.flush();
      activity_ = UsbTransferActivity::Rs485ToUsb;
    }
  }
}
