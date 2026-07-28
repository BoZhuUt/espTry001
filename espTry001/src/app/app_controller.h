#pragma once

#include "app_state.h"
#include "services/storage_service.h"
#include "sensor_discovery.h"
#include "sensor_profile.h"
#include "sensor_profile_store.h"
#include "sensor_profile_update.h"
#include "usb/usb_service.h"

// Owns application mode transitions and the ordered stop/reset/start sequence
// needed when a resource changes owner. It never draws UI.
class AppController {
public:
  AppController(AppState &state,
                StorageService &storageService,
                SensorDiscovery &sensorDiscovery,
                SensorProfilePoller &profilePoller,
                SensorProfileStore &profileStore,
                SensorProfileUpdater &profileUpdater,
                UsbService &usbService,
                Stream &usb,
                HardwareSerial &rs485,
                uint8_t &modbusResult,
                unsigned long &lastModbusTime,
                const SensorProfile *&activeProfile,
                bool &profileLookupComplete,
                uint8_t &parameterPage,
                bool *profileValueValid,
                size_t profileValueValidCount);

  const AppState &state() const { return state_; }

  bool togglePassMaster();
  void markMasterPolling();

  // CONFIG and LOG EXPORT take exclusive ownership of USB CDC. These methods
  // stop sensor/log activity and clear stale serial bytes before the selected
  // application protocol starts.
  bool enterConfigUpload();
  void cancelConfigUpload();
  bool enterLogExport(const char *firmwareVersion);
  void exitLogExport();

  void returnToPassThrough(UiPage page);
  void showPage(UiPage page);

private:
  void resetMasterSession();
  void drainSerialInputs();
  bool prepareExclusiveUsbMode(AppMode mode);

  AppState &state_;
  StorageService &storageService_;
  SensorDiscovery &sensorDiscovery_;
  SensorProfilePoller &profilePoller_;
  SensorProfileStore &profileStore_;
  SensorProfileUpdater &profileUpdater_;
  UsbService &usbService_;
  Stream &usb_;
  HardwareSerial &rs485_;
  uint8_t &modbusResult_;
  unsigned long &lastModbusTime_;
  const SensorProfile *&activeProfile_;
  bool &profileLookupComplete_;
  uint8_t &parameterPage_;
  bool *profileValueValid_;
  size_t profileValueValidCount_;
};
