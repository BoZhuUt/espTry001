#pragma once

#include <Arduino.h>

#include "app/app_state.h"
#include "services/rs485_service.h"
#include "sensor_profile_update.h"
#include "services/time_sync_server.h"
#include "usb/storage_usb_bridge.h"

enum class UsbTransferActivity : uint8_t {
  None,
  UsbToRs485,
  Rs485ToUsb,
};

// Owns USB protocol selection and transport. Exactly one protocol is advanced
// per loop according to AppMode, preventing ESPCFG1/ESPLOG1/ESPTIME1 bytes
// from being consumed by the Modbus RTU frame assembler.
class UsbService {
public:
  using LocalFrameHandler = void (*)(const uint8_t *frame, size_t length);

  static constexpr size_t kMaxRtuFrameLength = 256;
  static constexpr uint32_t kRtuFrameGapUs = 5000;

  UsbService(Stream &usb, Rs485Service &rs485Service,
             SensorProfileUpdater &profileUpdater,
             StorageUsbBridge &logExportBridge,
             TimeSyncServer &timeSyncServer,
             uint8_t localSlaveId,
             LocalFrameHandler localFrameHandler);

  // Route one non-blocking update to the protocol that owns USB in this mode.
  // Local Modbus requests remain available in MASTER; non-local forwarding is
  // enabled only in PASS.
  void update(AppMode mode);

  // Drop a partial RTU request whenever USB protocol ownership changes.
  void clearRtuFrame();
  void clearLogExportTransport();

  UsbTransferActivity takeTransferActivity();

private:
  void collectUsbBytes();
  void dispatchCompletedFrame(bool passThrough);
  void forwardRs485ToUsb(bool passThrough);

  Stream &usb_;
  Rs485Service &rs485Service_;
  SensorProfileUpdater &profileUpdater_;
  StorageUsbBridge &logExportBridge_;
  TimeSyncServer &timeSyncServer_;
  uint8_t localSlaveId_;
  LocalFrameHandler localFrameHandler_;

  uint8_t rtuFrame_[kMaxRtuFrameLength] = {0};
  size_t rtuFrameLength_ = 0;
  uint32_t lastUsbByteTimeUs_ = 0;
  UsbTransferActivity activity_ = UsbTransferActivity::None;
  uint8_t passResponse_[64] = {0};
  size_t passResponseLength_ = 0;
  size_t passResponseOffset_ = 0;
};
