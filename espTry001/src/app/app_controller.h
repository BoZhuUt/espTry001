#pragma once

#include "app_state.h"
#include "services/rs485_service.h"
#include "services/storage_service.h"
#include "sensor_profile_store.h"
#include "sensor_profile_update.h"
#include "services/time_sync_server.h"
#include "usb/usb_service.h"

// Owns application mode transitions and the ordered stop/reset/start sequence
// needed when a resource changes owner. It never draws UI.
class AppController {
public:
  AppController(AppState &state,
                StorageService &storageService,
                Rs485Service &rs485Service,
                SensorProfileStore &profileStore,
                SensorProfileUpdater &profileUpdater,
                TimeSyncServer &timeSyncServer,
                UsbService &usbService,
                Stream &usb,
                const SensorProfile *&activeProfile,
                bool &profileLookupComplete,
                uint8_t &parameterPage);

  const AppState &state() const { return state_; }

  bool cycleOperatingMode();
  void markMasterPolling();

  // CONFIG and LOG EXPORT take exclusive ownership of USB CDC. These methods
  // stop sensor/log activity and clear stale serial bytes before the selected
  // application protocol starts.
  bool enterConfigUpload();
  void cancelConfigUpload();
  bool enterLogExport(const char *firmwareVersion);
  void exitLogExport();

  /**
   * 停止日志与 RS485 后让 ESPTIME1 独占 USB，并开始新的校时会话。
   *
   * 返回 false 表示资源停止失败，调用者不应绘制 TIME SYNC 页面。
   */
  bool enterTimeSync();

  // 取消未完成的 ESPTIME1 命令，并恢复 PASS 与 System Info 页面。
  void exitTimeSync();

  bool beginCalibration(uint8_t slaveAddress,
                        const CalibrationProfile *profile,
                        uint8_t procedureIndex, const float *inputValues,
                        uint8_t inputCount);
  void exitCalibrationAndRediscover();

  void returnToPassThrough(UiPage page);
  void showPage(UiPage page);

private:
  void resetMasterSession();
  void drainSerialInputs();
  bool prepareExclusiveUsbMode(AppMode mode);

  AppState &state_;
  StorageService &storageService_;
  Rs485Service &rs485Service_;
  SensorProfileStore &profileStore_;
  SensorProfileUpdater &profileUpdater_;
  TimeSyncServer &timeSyncServer_;
  UsbService &usbService_;
  Stream &usb_;
  const SensorProfile *&activeProfile_;
  bool &profileLookupComplete_;
  uint8_t &parameterPage_;
};
