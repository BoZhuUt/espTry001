#pragma once

#include <Arduino.h>

enum class UiSdState : uint8_t {
  Missing,
  Ready,
  Error,
};

struct SystemInfoView {
  const char *firmwareVersion = "";
  const char *profileSource = "";
  const char *configVersion = "";
  bool profileFallback = false;
  const char *profileError = "";
  UiSdState sdState = UiSdState::Missing;
  const char *sdStateText = "";
  uint64_t sdFreeBytes = 0;
  bool recording = false;
  uint32_t logRecordCount = 0;
  uint8_t storageQueueDepth = 0;
  uint8_t storageQueuePeak = 0;
  uint32_t storageDroppedSnapshots = 0;
  UBaseType_t storageTaskStackHighWater = 0;
  const char *sdError = "";
};

enum class UiConfigState : uint8_t {
  Active,
  Success,
  Failed,
};

struct ConfigUpdateView {
  const char *statusText = "";
  uint32_t expectedLength = 0;
  uint32_t receivedLength = 0;
  UiConfigState state = UiConfigState::Active;
  const char *errorText = "";
};

enum class UiLogExportState : uint8_t {
  Active,
  WaitingCommand,
  Failed,
};

struct LogExportView {
  const char *statusText = "";
  UiLogExportState state = UiLogExportState::Active;
  const char *currentFile = "";
  uint32_t transferOffset = 0;
  uint32_t transferTotal = 0;
  const char *errorText = "";
};

constexpr uint8_t kUiSensorRowsPerPage = 4;

struct SensorParameterView {
  const char *label = "";
  float value = 0.0f;
  bool valid = false;
  uint8_t decimals = 0;
};

struct SensorInfoView {
  uint8_t slaveAddress = 0;
  uint32_t productNum = 0;
  const char *deviceName = "Unsupported PN";
  bool supported = false;
  SensorParameterView rows[kUiSensorRowsPerPage];
  uint8_t rowCount = 0;
  uint8_t pageNumber = 1;
  uint8_t pageCount = 0;
};

enum class UiDiscoveryState : uint8_t {
  Idle,
  DiscoveringAddress,
  DiscoveringSpecialAddress,
  ReadingIdentity,
  ReadingProductNumber,
  Success,
  Failed,
};

struct DashboardView {
  bool masterMode = false;
  float channel1Ma = 0.0f;
  float channel2Ma = 0.0f;
  UiDiscoveryState discoveryState = UiDiscoveryState::Idle;
  uint8_t attempt = 0;
  uint8_t maxAttempts = 0;
  uint8_t slaveAddress = 0;
  uint32_t productNum = 0;
  const char *deviceName = "Unsupported PN";
  const char *errorText = "";
  bool exceptionResponse = false;
  uint8_t exceptionCode = 0;
};

// Owns all low-level drawing calls. Business code supplies state and decides
// what an action means; UiManager only renders the current 1.47-inch layout.
class UiManager {
public:
  void clearScreen();
  void flush();
  void drawDashboardStatic(bool masterMode);
  void drawModeStatus(bool masterMode);
  void drawPassModeHint();
  void clearTransferStatus(bool masterMode);
  void showTransferStatus(const char *text, uint16_t color);
  void drawToolsMenuPage();
  void drawSystemInfoPage(const SystemInfoView &view);
  void drawConfigUpdatePage(const ConfigUpdateView &view);
  void drawLogExportPage(const LogExportView &view);
  void drawSensorInfoPage(const SensorInfoView &view);
  void drawSensorParameterArea(const SensorInfoView &view);
  void drawDashboardDynamic(const DashboardView &view);
};
