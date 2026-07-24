#include "app_controller.h"

AppController::AppController(
    AppState &state,
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
    size_t profileValueValidCount)
    : state_(state),
      dataLogger_(dataLogger),
      sensorDiscovery_(sensorDiscovery),
      profilePoller_(profilePoller),
      profileStore_(profileStore),
      profileUpdater_(profileUpdater),
      logExportServer_(logExportServer),
      sdStorage_(sdStorage),
      usb_(usb),
      rs485_(rs485),
      usbModbusFrameLength_(usbModbusFrameLength),
      modbusResult_(modbusResult),
      lastModbusTime_(lastModbusTime),
      activeProfile_(activeProfile),
      profileLookupComplete_(profileLookupComplete),
      parameterPage_(parameterPage),
      profileValueValid_(profileValueValid),
      profileValueValidCount_(profileValueValidCount) {}

bool AppController::togglePassMaster() {
  // A logging session must be closed before its sensor identity is reset.
  dataLogger_.stop();

  state_.mode = state_.isMaster()
                    ? AppMode::PassThrough
                    : AppMode::MasterDiscovery;

  drainSerialInputs();
  resetMasterSession();

  if (state_.isMaster()) {
    sensorDiscovery_.start();
  } else {
    sensorDiscovery_.cancel();
  }
  return state_.isMaster();
}

void AppController::markMasterPolling() {
  // Ignore stale discovery completion after an exclusive USB mode took over.
  if (state_.mode == AppMode::MasterDiscovery) {
    state_.mode = AppMode::MasterPolling;
  }
}

void AppController::enterConfigUpload() {
  prepareExclusiveUsbMode(AppMode::ConfigUpload);

  // Start ESPCFG1 only after old Modbus bytes have been discarded. A partial
  // RTU frame must never be interpreted as an upload request.
  profileUpdater_.begin(profileStore_);
}

void AppController::cancelConfigUpload() {
  profileUpdater_.cancel();
}

void AppController::enterLogExport(const char *firmwareVersion) {
  prepareExclusiveUsbMode(AppMode::LogExport);

  // prepareExclusiveUsbMode() has already flushed and closed the active CSV,
  // so LogExportServer can safely open files on the same SD card.
  logExportServer_.begin(sdStorage_, profileStore_, dataLogger_,
                         firmwareVersion);
  state_.page = UiPage::LogExport;
}

void AppController::exitLogExport() {
  // Close any active LIST/STAT/GET file handle before PASS regains USB.
  logExportServer_.cancel();
  returnToPassThrough(UiPage::SystemInfo);
}

void AppController::returnToPassThrough(UiPage page) {
  state_.mode = AppMode::PassThrough;
  state_.page = page;
}

void AppController::showPage(UiPage page) {
  state_.page = page;
}

void AppController::resetMasterSession() {
  usbModbusFrameLength_ = 0;
  modbusResult_ = 0xFF;
  lastModbusTime_ = 0;
  activeProfile_ = nullptr;
  profilePoller_.cancel();
  profileLookupComplete_ = false;
  parameterPage_ = 0;
  if (profileValueValid_ != nullptr && profileValueValidCount_ > 0) {
    memset(profileValueValid_, 0,
           profileValueValidCount_ * sizeof(profileValueValid_[0]));
  }
}

void AppController::drainSerialInputs() {
  while (usb_.available()) usb_.read();
  while (rs485_.available()) rs485_.read();
}

void AppController::prepareExclusiveUsbMode(AppMode mode) {
  // Transition order matters:
  // 1. Flush/close CSV while its sensor identity is still valid.
  // 2. Publish the exclusive mode so main stops Modbus processing.
  // 3. Stop RS485 discovery/polling and forget the active Profile.
  // 4. Drop partial USB/RTU bytes before ESPCFG1 or ESPLOG1 starts.
  dataLogger_.stop();
  state_.mode = mode;
  sensorDiscovery_.cancel();
  profilePoller_.cancel();
  activeProfile_ = nullptr;
  profileLookupComplete_ = false;
  usbModbusFrameLength_ = 0;
  drainSerialInputs();
}
