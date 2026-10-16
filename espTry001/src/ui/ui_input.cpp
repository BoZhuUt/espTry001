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
      if (y >= 60 && y < 120) return UiAction::EnterConfigUpload;
      if (y >= 130 && y < 190) return UiAction::EnterLogExport;
      if (y >= 200 && y < 260) return UiAction::EnterTimeSync;
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
      break;
  }

  return UiAction::None;
}
