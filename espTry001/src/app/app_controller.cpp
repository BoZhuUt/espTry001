#include "app_controller.h"

AppController::AppController(
    AppState &state,
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
    size_t profileValueValidCount)
    : state_(state),
      storageService_(storageService),
      sensorDiscovery_(sensorDiscovery),
      profilePoller_(profilePoller),
      profileStore_(profileStore),
      profileUpdater_(profileUpdater),
      usbService_(usbService),
      usb_(usb),
      rs485_(rs485),
      modbusResult_(modbusResult),
      lastModbusTime_(lastModbusTime),
      activeProfile_(activeProfile),
      profileLookupComplete_(profileLookupComplete),
      parameterPage_(parameterPage),
      profileValueValid_(profileValueValid),
      profileValueValidCount_(profileValueValidCount) {}

bool AppController::togglePassMaster() {
  // A logging session must be closed before its sensor identity is reset.
  if (!storageService_.stopLoggingAndWait()) {
    state_.mode = AppMode::Fault;
    return false;
  }

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

bool AppController::enterConfigUpload() {
  if (!prepareExclusiveUsbMode(AppMode::ConfigUpload)) return false;

  // Start ESPCFG1 only after old Modbus bytes have been discarded. A partial
  // RTU frame must never be interpreted as an upload request.
  profileUpdater_.begin(profileStore_);
  return true;
}

void AppController::cancelConfigUpload() {
  profileUpdater_.cancel();
}

bool AppController::enterLogExport(const char *firmwareVersion) {
  if (!prepareExclusiveUsbMode(AppMode::LogExport)) return false;

  // StorageTask has acknowledged flush/close before it starts ESPLOG1.
  if (!storageService_.beginLogExport(firmwareVersion)) {
    state_.mode = AppMode::Fault;
    return false;
  }
  state_.page = UiPage::LogExport;
  return true;
}

void AppController::exitLogExport() {
  // Close any active LIST/STAT/GET file handle before PASS regains USB.
  if (!storageService_.cancelLogExportAndWait()) {
    state_.mode = AppMode::Fault;
    return;
  }
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
  usbService_.clearRtuFrame();
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

bool AppController::prepareExclusiveUsbMode(AppMode mode) {
  // Transition order matters:
  // 1. Flush/close CSV while its sensor identity is still valid.
  // 2. Publish the exclusive mode so main stops Modbus processing.
  // 3. Stop RS485 discovery/polling and forget the active Profile.
  // 4. Drop partial USB/RTU bytes before ESPCFG1 or ESPLOG1 starts.
  if (!storageService_.stopLoggingAndWait()) {
    state_.mode = AppMode::Fault;
    return false;
  }
  state_.mode = mode;
  sensorDiscovery_.cancel();
  profilePoller_.cancel();
  activeProfile_ = nullptr;
  profileLookupComplete_ = false;
  usbService_.clearRtuFrame();
  usbService_.clearLogExportTransport();
  drainSerialInputs();
  return true;
}
