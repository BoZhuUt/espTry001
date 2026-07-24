#pragma once

#include "app_state.h"
#include "data_logger.h"
#include "log_export_server.h"
#include "sd_storage.h"
#include "sensor_discovery.h"
#include "sensor_profile.h"
#include "sensor_profile_store.h"
#include "sensor_profile_update.h"

// Owns application mode transitions and the ordered stop/reset/start sequence
// needed when a resource changes owner. It never draws UI.
class AppController {
public:
  AppController(AppState &state,
                DataLogger &dataLogger,
                SensorDiscovery &sensorDiscovery,
                SensorProfilePoller &profilePoller,
                SensorProfileStore &profileStore,
                SensorProfileUpdater &profileUpdater,
                LogExportServer &logExportServer,
                SdStorage &sdStorage,
                Stream &usb,
                HardwareSerial &rs485,
                size_t &usbModbusFrameLength,
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
  void enterConfigUpload();
  void cancelConfigUpload();
  void enterLogExport(const char *firmwareVersion);
  void exitLogExport();

  void returnToPassThrough(UiPage page);
  void showPage(UiPage page);

private:
  void resetMasterSession();
  void drainSerialInputs();
  void prepareExclusiveUsbMode(AppMode mode);

  AppState &state_;
  DataLogger &dataLogger_;
  SensorDiscovery &sensorDiscovery_;
  SensorProfilePoller &profilePoller_;
  SensorProfileStore &profileStore_;
  SensorProfileUpdater &profileUpdater_;
  LogExportServer &logExportServer_;
  SdStorage &sdStorage_;
  Stream &usb_;
  HardwareSerial &rs485_;
  size_t &usbModbusFrameLength_;
  uint8_t &modbusResult_;
  unsigned long &lastModbusTime_;
  const SensorProfile *&activeProfile_;
  bool &profileLookupComplete_;
  uint8_t &parameterPage_;
  bool *profileValueValid_;
  size_t profileValueValidCount_;
};
