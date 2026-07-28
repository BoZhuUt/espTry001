#include "app_controller.h"

AppController::AppController(
    AppState &state,
    StorageService &storageService,
    Rs485Service &rs485Service,
    SensorProfileStore &profileStore,
    SensorProfileUpdater &profileUpdater,
    UsbService &usbService,
    Stream &usb,
    const SensorProfile *&activeProfile,
    bool &profileLookupComplete,
    uint8_t &parameterPage)
    : state_(state),
      storageService_(storageService),
      rs485Service_(rs485Service),
      profileStore_(profileStore),
      profileUpdater_(profileUpdater),
      usbService_(usbService),
      usb_(usb),
      activeProfile_(activeProfile),
      profileLookupComplete_(profileLookupComplete),
      parameterPage_(parameterPage) {}

bool AppController::togglePassMaster() {
  // A logging session must be closed before its sensor identity is reset.
  if (!storageService_.stopLoggingAndWait()) {
    state_.mode = AppMode::Fault;
    return false;
  }

  bool enterMaster = !state_.isMaster();
  if (!rs485Service_.stopAndWait()) {
    state_.mode = AppMode::Fault;
    return false;
  }
  state_.mode =
      enterMaster ? AppMode::MasterDiscovery : AppMode::PassThrough;

  drainSerialInputs();
  resetMasterSession();

  bool commandAccepted = enterMaster ? rs485Service_.startDiscovery()
                                     : rs485Service_.enterPassThrough();
  if (!commandAccepted) {
    state_.mode = AppMode::Fault;
    return false;
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
  if (!rs485Service_.enterPassThrough()) {
    state_.mode = AppMode::Fault;
    return;
  }
  state_.mode = AppMode::PassThrough;
  state_.page = page;
}

void AppController::showPage(UiPage page) {
  state_.page = page;
}

void AppController::resetMasterSession() {
  usbService_.clearRtuFrame();
  activeProfile_ = nullptr;
  profileLookupComplete_ = false;
  parameterPage_ = 0;
}

void AppController::drainSerialInputs() {
  while (usb_.available()) usb_.read();
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
  if (!rs485Service_.stopAndWait()) {
    state_.mode = AppMode::Fault;
    return false;
  }
  state_.mode = mode;
  activeProfile_ = nullptr;
  profileLookupComplete_ = false;
  usbService_.clearRtuFrame();
  usbService_.clearLogExportTransport();
  drainSerialInputs();
  return true;
}
