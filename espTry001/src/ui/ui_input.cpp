#include "ui_input.h"

UiAction resolveTouchAction(UiPage page, int x, int y,
                            const UiInputContext &context) {
  (void)x;  // Both portrait layouts use full-width horizontal hit regions.

  if (context.configMode) {
    return (y > 0 && y < 45) ? UiAction::CancelConfigAndRestart
                             : UiAction::None;
  }

  switch (page) {
    case UiPage::Dashboard:
      if (y >= 40 && y < 80) return UiAction::TogglePassMaster;
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
      if (y >= 200 && y < 260) return UiAction::BackToSystemInfo;
      break;

    case UiPage::LogExport:
      if (y > 0 && y < 40) return UiAction::BackToSystemInfo;
      break;

    case UiPage::SensorInfo:
      if (y >= 280) return UiAction::NextSensorParameterPage;
      if (y > 0 && y < 40) return UiAction::BackToDashboard;
      break;
  }

  return UiAction::None;
}
