#include "usb_service.h"
#include <USB.h>
#include <tusb.h>

namespace { UsbService *cdcOwner = nullptr; }

#if ARDUINO_USB_MODE != 0 || !ARDUINO_USB_CDC_ON_BOOT
#error "UsbService requires the built-in TinyUSB CDC Serial transport"
#endif

void UsbService::beginCdc() {
  // Normal serial tools must not reboot the bridge when toggling DTR/RTS.
  Serial.enableReboot(false);
  cdcOwner = this;
  Serial.onEvent([](void *, esp_event_base_t base, int32_t id, void *data) {
    cdcEvent(cdcOwner, base, id, data);
  });
  USB.onEvent([](void *, esp_event_base_t base, int32_t id, void *data) {
    cdcEvent(cdcOwner, base, id, data);
  });
  // CDC starts before setup() in Arduino. Recover settings received before
  // our event handler was registered, without overwriting a newer callback.
  cdc_line_coding_t coding;
  tud_cdc_get_line_coding(&coding);
  portENTER_CRITICAL(&codingMux_);
  if (!hostCodingReceived_ && Serial.baudRate() != 0) {
    hostCoding_ = {coding.bit_rate, coding.stop_bits,
                   coding.parity, coding.data_bits};
    ++hostCodingRevision_;
  }
  portEXIT_CRITICAL(&codingMux_);
}

void UsbService::cdcEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  // The wrapper routes the single CDC transport to its UsbService owner.
  auto *self = static_cast<UsbService *>(arg);
  auto *event = static_cast<arduino_usb_cdc_event_data_t *>(data);
  portENTER_CRITICAL(&self->codingMux_);
  if (base == ARDUINO_USB_EVENTS) {
    if (id == ARDUINO_USB_STOPPED_EVENT) ++self->hostCodingRevision_;
  } else if (id == ARDUINO_USB_CDC_LINE_CODING_EVENT) {
    self->hostCodingReceived_ = true;
    self->hostCoding_ = {event->line_coding.bit_rate,
                         event->line_coding.stop_bits,
                         event->line_coding.parity,
                         event->line_coding.data_bits};
    ++self->hostCodingRevision_;
  }
  // DTR/RTS do not change UART settings or interrupt an in-flight frame.
  portEXIT_CRITICAL(&self->codingMux_);
}

bool UsbService::syncPassCoding(AppMode mode) {
  CdcLineCoding coding;
  uint32_t revision;
  portENTER_CRITICAL(&codingMux_);
  coding = hostCoding_;
  revision = hostCodingRevision_;
  portEXIT_CRITICAL(&codingMux_);
  if (revision != loopCodingRevision_ || mode != previousMode_) {
    if (mode == AppMode::PassThrough || previousMode_ == AppMode::PassThrough) {
      clearRtuFrame();
    }
    loopCoding_ = coding;
    loopCodingRevision_ = revision;
    previousMode_ = mode;
  }
  if (submittedCodingRevision_ != revision) {
    if (!rs485Service_.setPassLineCoding(coding, revision)) return false;
    submittedCodingRevision_ = revision;
  }
  if (mode != AppMode::PassThrough) return true;
  Rs485Service::Status status;
  rs485Service_.getStatus(status);
  return status.mode == Rs485Service::Mode::PassThrough &&
         status.passCodingRevision == revision;
}

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
  if (mode == AppMode::MobileUpgrade) {
    // Offline sender owns both raw queues. Never pump PC CDC into them.
    while (usb_.available()) usb_.read();
    return;
  }
  const bool codingApplied = syncPassCoding(mode);
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
  if (mode == AppMode::UpgradePass) {
    pumpRawPassThrough();
    return;
  }
  if (mode == AppMode::Calibration) {
    // Serial1 is exclusively owned by CalibrationExecutor. Discarding USB
    // input here prevents queued gateway traffic from executing afterwards.
    while (usb_.available()) usb_.read();
    clearRtuFrame();
    return;
  }
  bool passThrough = mode == AppMode::PassThrough;
  // Leave new host bytes in CDC until Rs485Task has applied the settings.
  // Unsupported settings still allow address 247 diagnostic requests.
  if (passThrough && !codingApplied) return;
  passThrough = passThrough && loopCoding_.supported();
  collectUsbBytes();

  if (rtuFrameLength_ > 0 &&
      (uint32_t)(micros() - lastUsbByteTimeUs_) >= loopCoding_.frameGapUs()) {
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
  rawResponseLength_ = 0;
  rawResponseOffset_ = 0;
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
  portENTER_CRITICAL(&codingMux_);
  const bool codingChanged = hostCodingRevision_ != loopCodingRevision_;
  portEXIT_CRITICAL(&codingMux_);
  if (passThrough && codingChanged) {
    clearRtuFrame();
    return;
  }
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

void UsbService::pumpRawPassThrough() {
  // Apply backpressure before reading CDC so no byte is removed from USB
  // unless the USB->UART stream has room for it.
  uint8_t request[kRawUsbChunkSize];
  size_t usbAvailable = usb_.available();
  size_t streamSpace = rs485Service_.rawRequestSpace();
  size_t requested = min(sizeof(request), min(usbAvailable, streamSpace));
  size_t requestLength = 0;
  while (requestLength < requested && usb_.available()) {
    int value = usb_.read();
    if (value < 0) break;
    request[requestLength++] = static_cast<uint8_t>(value);
  }
  if (requestLength > 0) {
    size_t accepted = rs485Service_.enqueueRawBytes(request, requestLength);
    if (accepted > 0) activity_ = UsbTransferActivity::UsbToRs485;
    // Only this task produces rawRequestBuffer_, and requested never exceeds
    // its reported space, so a partial enqueue would indicate an invariant
    // violation. The service still records it as dropped diagnostics.
  }

  if (rawResponseOffset_ >= rawResponseLength_) {
    rawResponseLength_ = rs485Service_.readRawResponse(
        rawResponse_, sizeof(rawResponse_));
    rawResponseOffset_ = 0;
  }
  if (rawResponseOffset_ < rawResponseLength_) {
    size_t accepted = usb_.write(
        rawResponse_ + rawResponseOffset_,
        rawResponseLength_ - rawResponseOffset_);
    if (accepted > 0) {
      rawResponseOffset_ += accepted;
      usb_.flush();
      activity_ = UsbTransferActivity::Rs485ToUsb;
    }
  }
}
