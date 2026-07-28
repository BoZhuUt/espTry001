#pragma once

#include <Arduino.h>

#include "ui/ui_types.h"

// Application operating mode. Only one value can be active, which prevents
// contradictory combinations such as PASS and CONFIG owning USB together.
enum class AppMode : uint8_t {
  PassThrough,
  MasterDiscovery,
  MasterPolling,
  ConfigUpload,
  LogExport,
  Fault,
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

enum class AppStorageState : uint8_t {
  Missing,
  Ready,
  Error,
};

// System Info snapshot. It owns all text copied from ProfileStore, SdStorage,
// and build metadata so the UI never observes half-updated module buffers.
struct SystemState {
  char firmwareVersion[24] = "";
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

struct AppState {
  AppMode mode = AppMode::PassThrough;
  UiPage page = UiPage::Dashboard;
  DashboardState dashboard;
  SensorState sensor;
  SystemState system;
  ConfigUpdateState configUpdate;
  LogExportState logExport;

  bool isMaster() const {
    return mode == AppMode::MasterDiscovery ||
           mode == AppMode::MasterPolling;
  }

  bool isConfigUpload() const {
    return mode == AppMode::ConfigUpload;
  }

  bool isLogExport() const {
    return mode == AppMode::LogExport;
  }
};
