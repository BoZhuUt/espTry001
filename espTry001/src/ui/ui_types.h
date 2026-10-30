#pragma once

#include <Arduino.h>

enum class UiPage : uint8_t {
  Dashboard,
  SystemInfo,
  SensorInfo,
  ToolsMenu,
  LogExport,
  TimeSync,
  Calibration,
  MobileUpgrade,
};

// Semantic actions emitted by the UI input layer. Coordinates and screen
// geometry stay in ui_input; business effects stay in main/AppController.
enum class UiAction : uint8_t {
  None,
  CancelConfigAndRestart,
  CycleOperatingMode,
  OpenSensorInfo,
  OpenSystemInfo,
  OpenToolsMenu,
  EnterConfigUpload,
  EnterLogExport,
  EnterTimeSync,
  RetryTimeSync,
  OpenCalibration,
  BackToSystemInfo,
  NextSensorParameterPage,
  BackToDashboard,
  EnterFirmwareDownload,
  OpenMobileUpgrade,
};
