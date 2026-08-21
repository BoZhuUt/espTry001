#pragma once

#include <Arduino.h>

#include "ui/ui_types.h"
#include "calibration_executor.h"

// Application operating mode. Only one value can be active, which prevents
// contradictory combinations such as PASS and CONFIG owning USB together.
enum class AppMode : uint8_t {
  PassThrough,
  MasterDiscovery,
  MasterPolling,
  ConfigUpload,
  LogExport,
  TimeSync,
  Fault,
  Precheck,
  // Append new modes to preserve the numeric values exposed by diagnostics.
  UpgradePass,
  Calibration,
};

enum class AppDiscoveryPhase : uint8_t {
  Idle,
  DiscoveringAddress,
  DiscoveringSpecialAddress,
  ReadingIdentity,
  ReadingProductNumber,
  Success,
  Failed,
};

// Fixed-size Dashboard snapshot. Character arrays intentionally own their
// contents so a future UI task never keeps pointers into ProfileStore or a
// protocol object's mutable internal buffer.
struct DashboardState {
  float channel1Ma = 0.0f;
  float channel2Ma = 0.0f;
  bool rtcSupported = false;
  bool rtcValid = false;
  char rtcTimeText[9] = "--:--:--";
  AppDiscoveryPhase discoveryPhase = AppDiscoveryPhase::Idle;
  uint8_t attempt = 0;
  uint8_t maxAttempts = 0;
  uint8_t slaveAddress = 0;
  uint32_t productNum = 0;
  char deviceName[32] = "Unsupported PN";
  char errorText[64] = "";
  bool exceptionResponse = false;
  uint8_t exceptionCode = 0;
};

constexpr uint8_t kAppMaxSensorFields = 16;
constexpr size_t kAppSensorNameLength = 32;
constexpr size_t kAppSensorLabelLength = 32;

struct SensorFieldState {
  char label[kAppSensorLabelLength] = "";
  float value = 0.0f;
  bool valid = false;
  uint8_t decimals = 0;
};

// Complete, fixed-size Sensor Info snapshot. Copying all 16 fields keeps UI
// independent from the active JSON Profile and the poller's mutable arrays.
struct SensorState {
  uint8_t slaveAddress = 0;
  uint32_t productNum = 0;
  char deviceName[kAppSensorNameLength] = "Unsupported PN";
  bool supported = false;
  SensorFieldState fields[kAppMaxSensorFields];
  uint8_t fieldCount = 0;
  uint8_t parameterPage = 0;
  uint32_t scanSequence = 0;
};

struct PrecheckState {
  static constexpr uint8_t kSlaveAddress = 15;

  float s470Raw = 0.0f;
  float t365Raw = 0.0f;
  bool s470Valid = false;
  bool t365Valid = false;
  uint32_t scanSequence = 0;
  uint8_t lastModbusError = 0xFF;
};

enum class AppStorageState : uint8_t {
  Missing,
  Ready,
  Error,
};

// System Info snapshot. It owns all text copied from ProfileStore, SdStorage,
// and build metadata so the UI never observes half-updated module buffers.
struct SystemState {
  char firmwareVersion[24] = "";
  char resetReasonText[20] = "UNKNOWN";
  uint8_t resetReasonCode = 0;
  uint8_t rawResetReasonCpu0 = 0;
  uint8_t rawResetReasonCpu1 = 0;
  bool resetReasonFault = false;
  char profileSource[16] = "";
  char configVersion[32] = "";
  bool profileFallback = false;
  char profileError[64] = "";
  AppStorageState storageState = AppStorageState::Missing;
  char storageStateText[16] = "";
  uint64_t storageFreeBytes = 0;
  bool recording = false;
  uint32_t logRecordCount = 0;
  uint8_t storageQueueDepth = 0;
  uint8_t storageQueuePeak = 0;
  uint32_t storageDroppedSnapshots = 0;
  UBaseType_t storageTaskStackHighWater = 0;
  char storageError[64] = "";
  bool rtcSupported = false;
  bool rtcValid = false;
  bool rtcHoldover = false;
  char rtcStatusText[20] = "";
  char rtcDateTimeText[20] = "";
  char ntpStatusText[20] = "";
};

enum class AppConfigUpdateState : uint8_t {
  Active,
  Success,
  Failed,
};

struct ConfigUpdateState {
  char statusText[64] = "";
  uint32_t expectedLength = 0;
  uint32_t receivedLength = 0;
  AppConfigUpdateState state = AppConfigUpdateState::Active;
  char errorText[96] = "";
};

enum class AppLogExportState : uint8_t {
  Active,
  WaitingCommand,
  Failed,
};

struct LogExportState {
  char statusText[64] = "";
  AppLogExportState state = AppLogExportState::Active;
  char currentFile[80] = "";
  uint32_t transferOffset = 0;
  uint32_t transferTotal = 0;
  char errorText[96] = "";
};

struct TimeSyncState {
  char statusText[32] = "READY";
  bool rtcValid = false;
  char rtcDateTimeText[32] = "";
  char timeSourceText[8] = "NONE";
  char ntpStatusText[20] = "";
  uint32_t retrySeconds = 0;
  char wifiSsid[33] = "";
  char wifiPassword[64] = "";
  char errorText[64] = "";
};

enum class CalibrationUiPhase : uint8_t {
  ProcedureList,
  NumericInput,
  Confirm,
  Running,
  Finished,
};

struct CalibrationState {
  CalibrationUiPhase phase = CalibrationUiPhase::ProcedureList;
  uint8_t procedurePage = 0;
  uint8_t selectedProcedure = 0;
  uint8_t currentInput = 0;
  float inputValues[CalibrationExecutor::kMaxProcedureInputs] = {0};
  char numericText[20] = "";
  bool numericDefaultShown = false;
  char statusText[96] = "";
  CalibrationExecutor::State executorState = CalibrationExecutor::State::Idle;
  uint8_t commandIndex = 0;
  uint8_t commandCount = 0;
};

struct AppState {
  AppMode mode = AppMode::PassThrough;
  UiPage page = UiPage::Dashboard;
  DashboardState dashboard;
  SensorState sensor;
  PrecheckState precheck;
  SystemState system;
  ConfigUpdateState configUpdate;
  LogExportState logExport;
  TimeSyncState timeSync;
  CalibrationState calibration;

  bool isMaster() const {
    return mode == AppMode::MasterDiscovery ||
           mode == AppMode::MasterPolling;
  }

  bool isPrecheck() const {
    return mode == AppMode::Precheck;
  }

  bool isPassThrough() const {
    return mode == AppMode::PassThrough;
  }

  bool isUpgradePass() const {
    return mode == AppMode::UpgradePass;
  }

  bool isConfigUpload() const {
    return mode == AppMode::ConfigUpload;
  }

  bool isLogExport() const {
    return mode == AppMode::LogExport;
  }

  bool isTimeSync() const {
    return mode == AppMode::TimeSync;
  }

  bool isCalibration() const { return mode == AppMode::Calibration; }
};
