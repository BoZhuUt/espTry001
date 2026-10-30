#include "ui_input.h"

UiAction resolveTouchAction(UiPage page, int x, int y,
                            const UiInputContext &context) {
  if (context.configMode) {
    return (y > 0 && y < 45) ? UiAction::CancelConfigAndRestart
                             : UiAction::None;
  }

  switch (page) {
    case UiPage::Dashboard:
      if (y >= 40 && y < 80) return UiAction::CycleOperatingMode;
      if (y >= 280 && context.sensorDetailsAvailable) {
        return UiAction::OpenSensorInfo;
      }
      if (y > 0 && y < 40) return UiAction::OpenSystemInfo;
      break;

    case UiPage::SystemInfo:
      if (y >= 270) return UiAction::OpenToolsMenu;
      if (y > 0 && y < 80) return UiAction::BackToDashboard;
      break;

    case UiPage::ToolsMenu:
      if (y > 0 && y < 40) return UiAction::BackToSystemInfo;
      if (y >= 55 && y < 105) return UiAction::EnterConfigUpload;
      if (y >= 110 && y < 160) return UiAction::EnterLogExport;
      if (y >= 165 && y < 215) return UiAction::EnterTimeSync;
      if (y >= 220 && y < 270) return UiAction::OpenMobileUpgrade;
      if (y >= 272 && y < 320) return UiAction::EnterFirmwareDownload;
      break;

    case UiPage::LogExport:
      if (y > 0 && y < 40) return UiAction::BackToSystemInfo;
      break;

    case UiPage::TimeSync:
      if (y > 0 && y < 40) return UiAction::BackToSystemInfo;
      if (y >= 215 && y < 258) return UiAction::RetryTimeSync;
      break;

    case UiPage::SensorInfo:
      if (y >= 280) {
        if (context.calibrationAvailable && x >= 120) {
          return UiAction::OpenCalibration;
        }
        return UiAction::NextSensorParameterPage;
      }
      if (y > 0 && y < 40) return UiAction::BackToDashboard;
      break;

    case UiPage::Calibration:
    case UiPage::MobileUpgrade:
      break;
  }

  return UiAction::None;
}
