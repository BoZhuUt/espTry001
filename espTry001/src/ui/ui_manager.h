#pragma once

#include <Arduino.h>

enum class UiSdState : uint8_t {
  Missing,
  Ready,
  Error,
};

struct SystemInfoView {
  const char *firmwareVersion = "";
  const char *resetReasonText = "UNKNOWN";
  uint8_t resetReasonCode = 0;
  uint8_t rawResetReasonCpu0 = 0;
  uint8_t rawResetReasonCpu1 = 0;
  bool resetReasonFault = false;
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
  bool rtcSupported = false;
  bool rtcValid = false;
  bool rtcHoldover = false;
  const char *rtcStatusText = "";
  const char *rtcDateTimeText = "";
  const char *ntpStatusText = "";
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

struct TimeSyncView {
  const char *statusText = "";
  bool rtcValid = false;
  const char *rtcDateTimeText = "";
  const char *timeSourceText = "NONE";
  const char *ntpStatusText = "";
  const char *maskedSsid = "";
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

enum class UiOperatingMode : uint8_t {
  Pass,
  Master,
  Precheck,
  UpgradePass,
};

struct DashboardView {
  UiOperatingMode mode = UiOperatingMode::Pass;
  float channel1Ma = 0.0f;
  float channel2Ma = 0.0f;
  bool rtcSupported = false;
  bool rtcValid = false;
  const char *rtcTimeText = "--:--:--";
  UiDiscoveryState discoveryState = UiDiscoveryState::Idle;
  uint8_t attempt = 0;
  uint8_t maxAttempts = 0;
  uint8_t slaveAddress = 0;
  uint32_t productNum = 0;
  const char *deviceName = "Unsupported PN";
  const char *errorText = "";
  bool exceptionResponse = false;
  uint8_t exceptionCode = 0;
  float s470Raw = 0.0f;
  float t365Raw = 0.0f;
  bool s470Valid = false;
  bool t365Valid = false;
  uint32_t precheckScanSequence = 0;
  uint8_t precheckError = 0xFF;
  uint32_t rawUsbToUartBytes = 0;
  uint32_t rawUartToUsbBytes = 0;
  uint32_t rawUsbToUartDrops = 0;
  uint32_t rawUartToUsbDrops = 0;
  uint16_t rawRequestPeak = 0;
  uint16_t rawResponsePeak = 0;
};

// Owns all low-level drawing calls. Business code supplies state and decides
// what an action means; horizontal layout follows the selected board width.
class UiManager {
public:
  void clearScreen();
  void flush();
  void drawStartupPage(const char *firmwareVersion);
  void drawDashboardStatic(UiOperatingMode mode);
  void drawModeStatus(UiOperatingMode mode);
  void drawPassModeHint();
  void clearTransferStatus(UiOperatingMode mode);
  void showTransferStatus(const char *text, uint16_t color);
  void drawToolsMenuPage();
  void drawSystemInfoPage(const SystemInfoView &view);
  void drawConfigUpdatePage(const ConfigUpdateView &view);
  void drawLogExportPage(const LogExportView &view);
  void drawTimeSyncPage(const TimeSyncView &view);
  void drawSensorInfoPage(const SensorInfoView &view);
  void drawSensorParameterArea(const SensorInfoView &view);
  void drawDashboardDynamic(const DashboardView &view);
};
