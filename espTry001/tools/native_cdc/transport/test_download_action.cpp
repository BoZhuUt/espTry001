#include "ui/ui_input.h"
#include <assert.h>
#include <stdio.h>
int main() {
  UiInputContext context;
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 286, context) == UiAction::EnterFirmwareDownload);
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 230, context) == UiAction::EnterTimeSync);
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 150, context) == UiAction::EnterLogExport);
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 90, context) == UiAction::EnterConfigUpload);
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 266, context) == UiAction::None);
  assert(resolveTouchAction(UiPage::Dashboard, 20, 286, context) == UiAction::None);
  context.configMode = true;
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 286, context) == UiAction::None);
  assert(resolveTouchAction(UiPage::ToolsMenu, 20, 20, context) == UiAction::CancelConfigAndRestart);
  puts("PASS: firmware-download action is isolated to its Tools button");
}
