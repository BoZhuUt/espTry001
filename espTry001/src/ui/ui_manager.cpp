#include "ui_manager.h"

#include "board/board_config.h"
#include "board/display_driver.h"

#if defined(PYXIS_BOARD_WAVESHARE_28)
#include "assets/pyxis_logo_rgb565.h"
#endif

namespace {

constexpr int16_t kContentLeft = 5;
constexpr int16_t kContentRight = BoardConfig::kDisplayWidth - 7;
constexpr int16_t kWideAreaWidth = BoardConfig::kDisplayWidth - 7;
constexpr int16_t kButtonWidth = BoardConfig::kDisplayWidth - 10;
constexpr int16_t kProgressInnerWidth = BoardConfig::kDisplayWidth - 12;

int16_t centeredTextX(const char *text, uint8_t textSize) {
  const int16_t textWidth = strlen(text) * 6 * textSize;
  return max<int16_t>(kContentLeft,
                      (BoardConfig::kDisplayWidth - textWidth) / 2);
}

void drawToolsCard(int16_t y, uint16_t accent, const char *title,
                   const char *subtitle) {
  constexpr int16_t kCardLeft = 10;
  constexpr int16_t kCardWidth = 220;
  constexpr int16_t kCardHeight = 56;

  gfx->fillRect(kCardLeft, y, kCardWidth, kCardHeight, RGB565_BLACK);
  gfx->drawRect(kCardLeft, y, kCardWidth, kCardHeight, accent);
  gfx->fillRect(kCardLeft, y, 5, kCardHeight, accent);

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(centeredTextX(title, 2), y + 8);
  gfx->print(title);

  gfx->setTextSize(1);
  gfx->setTextColor(accent, RGB565_BLACK);
  gfx->setCursor(centeredTextX(subtitle, 1), y + 34);
  gfx->print(subtitle);
}

}  // namespace

void UiManager::clearScreen() {
  gfx->fillScreen(RGB565_BLACK);
}

void UiManager::flush() {
  gfx->flush();
}

void UiManager::drawStartupPage(const char *firmwareVersion) {
  // Display-tuned version of the brand blue. The source PNG remains unchanged;
  // this brighter shade avoids appearing black on the small IPS panel.
  constexpr uint16_t kPyxisBlue = 0x035A;  // RGB(0, 107, 214)
  gfx->fillScreen(RGB565_WHITE);

#if defined(PYXIS_BOARD_WAVESHARE_28)
  constexpr int16_t kLogoX =
      (BoardConfig::kDisplayWidth - UiAssets::kPyxisLogoWidth) / 2;
  constexpr int16_t kLogoY = 72;
  gfx->draw16bitRGBBitmap(kLogoX, kLogoY, UiAssets::kPyxisLogoRgb565,
                         UiAssets::kPyxisLogoWidth,
                         UiAssets::kPyxisLogoHeight);
#endif

  gfx->setTextSize(1);
  gfx->setTextColor(kPyxisBlue, RGB565_WHITE);
  gfx->setCursor(centeredTextX("ESP32-S3 485 UTILITY", 1), 158);
  gfx->print("ESP32-S3 485 UTILITY");

  gfx->setCursor(centeredTextX(firmwareVersion, 1), 181);
  gfx->print(firmwareVersion);

  gfx->setCursor(centeredTextX("INITIALIZING...", 1), 225);
  gfx->print("INITIALIZING...");
  gfx->flush();
}

void UiManager::drawDashboardStatic(UiOperatingMode mode) {
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("ESP32-S3 485");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_YELLOW);

  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(5, 50);
  gfx->printf("Mode: ");
  drawModeStatus(mode);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(5, 66);
  gfx->printf("485 Config: %s\n",
              mode == UiOperatingMode::UpgradePass
                  ? "9600,8N1"
                  : "9600,8E1");
  gfx->drawLine(kContentLeft, 78, kContentRight, 78, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 88);
  gfx->println("[ 4-20mA Sampling ]");
  gfx->drawLine(kContentLeft, 175, kContentRight, 175, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 185);
  gfx->println("[ Modbus RTU Data ]");

  if (mode == UiOperatingMode::Pass) drawPassModeHint();
}

void UiManager::drawModeStatus(UiOperatingMode mode) {
  gfx->fillRect(55, 40, BoardConfig::kDisplayWidth - 60, 25, RGB565_BLACK);
  gfx->setTextSize(2);
  if (mode == UiOperatingMode::Master) {
    gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
    gfx->setCursor(centeredTextX("MASTER", 2), 44);
    gfx->print("MASTER");
  } else if (mode == UiOperatingMode::Precheck) {
    gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
    gfx->setCursor(centeredTextX("PRECHECK", 2), 44);
    gfx->print("PRECHECK");
  } else if (mode == UiOperatingMode::UpgradePass) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->setCursor(centeredTextX("BOOT PASS", 2), 44);
    gfx->print("BOOT PASS");
  } else {
    gfx->setTextColor(RGB565_MAGENTA, RGB565_BLACK);
    gfx->setCursor(centeredTextX("PASS", 2), 44);
    gfx->print("PASS");
  }
}

void UiManager::drawPassModeHint() {
  gfx->fillRect(kContentLeft, 286, kWideAreaWidth, 29, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setTextSize(1);
  gfx->setCursor(centeredTextX("Tap PASS to switch", 1), 296);
  gfx->print("Tap PASS to switch");
}

void UiManager::clearTransferStatus(UiOperatingMode mode) {
  gfx->fillRect(kContentLeft, 252, kWideAreaWidth, 24, RGB565_BLACK);
  if (mode == UiOperatingMode::Pass) drawPassModeHint();
}

void UiManager::showTransferStatus(const char *text, uint16_t color) {
  gfx->fillRect(kContentLeft, 252, kWideAreaWidth, 20, color);
  gfx->setTextColor(RGB565_WHITE, color);
  gfx->setTextSize(1);
  gfx->setCursor(12, 258);
  gfx->print(text);
  gfx->flush();
}

void UiManager::drawToolsMenuPage() {
  gfx->fillScreen(RGB565_BLACK);

  if (BoardConfig::kDisplayWidth >= 240) {
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
    gfx->setCursor(10, 12);
    gfx->print("TOOLS MENU");

    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->setCursor(10, 35);
    gfx->print("DEVICE UTILITIES");
    gfx->drawLine(10, 49, BoardConfig::kDisplayWidth - 11, 49,
                  RGB565_DARKGREY);

    drawToolsCard(60, RGB565_CYAN, "CONFIG UPLOAD",
                  "Update sensor profiles");
    drawToolsCard(130, RGB565_GREEN, "LOG EXPORT",
                  "Download recorded data");
    drawToolsCard(200, RGB565_ORANGE, "TIME SYNC",
                  "Set RTC from computer");

    gfx->drawLine(10, 268, BoardConfig::kDisplayWidth - 11, 268,
                  RGB565_DARKGREY);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setCursor(centeredTextX("Connect PC via USB first", 1), 278);
    gfx->print("Connect PC via USB first");
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->setCursor(centeredTextX("Tap a card to continue", 1), 296);
    gfx->print("Tap a card to continue");
    gfx->flush();
    return;
  }

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("TOOLS MENU");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->setCursor(5, 50);
  gfx->print("Tap header to go back");

  gfx->fillRect(kContentLeft, 60, kButtonWidth, 56, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setTextSize(2);
  gfx->setCursor(15, 80);
  gfx->print("1.CONFIG");
  gfx->setCursor(15, 98);
  gfx->print("  UPLOAD");

  gfx->fillRect(kContentLeft, 130, kButtonWidth, 56, RGB565_DARKGREEN);
  gfx->setTextColor(RGB565_WHITE, RGB565_DARKGREEN);
  gfx->setTextSize(2);
  gfx->setCursor(15, 150);
  gfx->print("2.LOG");
  gfx->setCursor(15, 168);
  gfx->print("  EXPORT");

  gfx->fillRect(kContentLeft, 200, kButtonWidth, 56, RGB565_ORANGE);
  gfx->setTextColor(RGB565_BLACK, RGB565_ORANGE);
  gfx->setTextSize(2);
  gfx->setCursor(15, 220);
  gfx->print("3.TIME");
  gfx->setCursor(15, 238);
  gfx->print("  SYNC");

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->setCursor(15, 270);
  gfx->print("Connect PC tool via USB");
  gfx->setCursor(15, 285);
  gfx->print("before tapping LOG EXPORT");
  gfx->flush();
}

void UiManager::drawSystemInfoPage(const SystemInfoView &view) {
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("SYSTEM INFO");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(5, 60);
  gfx->println("Author: Bo");
  gfx->setCursor(5, 100);
  gfx->println(view.firmwareVersion);
  gfx->setCursor(5, 130);
  gfx->println("esp32-utility");

  gfx->setTextSize(1);
  gfx->setCursor(5, 150);
  gfx->setTextColor(view.resetReasonFault ? RGB565_RED : RGB565_GREEN,
                    RGB565_BLACK);
  gfx->printf("Reset:%s R%u/%u", view.resetReasonText,
              view.rawResetReasonCpu0, view.rawResetReasonCpu1);
  gfx->setCursor(5, 165);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("Profile: %s\n", view.profileSource);
  gfx->setCursor(5, 182);
  gfx->printf("Config: %s\n", view.configVersion);
  if (view.profileFallback) {
    gfx->setCursor(5, 192);
    gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
    gfx->printf("Fallback: %.18s", view.profileError);
  }

  if (view.rtcSupported) {
    gfx->setCursor(5, 202);
    if (view.rtcValid) {
      gfx->setTextColor(view.rtcHoldover ? RGB565_ORANGE : RGB565_GREEN,
                        RGB565_BLACK);
      gfx->printf("RTC: %s", view.rtcDateTimeText);
    } else {
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->printf("RTC: %s", view.rtcStatusText);
    }
  }

  gfx->setCursor(5, 217);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->printf("NTP: %s", view.ntpStatusText);

  gfx->setCursor(5, 232);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("SD: %s", view.sdStateText);
  if (view.sdState == UiSdState::Ready) {
    gfx->printf(" %.0fMB free", (double)(view.sdFreeBytes / 1048576));
    gfx->setCursor(5, 247);
    if (view.storageDroppedSnapshots > 0) {
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->printf("Q:%u/%u Lost:%lu", view.storageQueueDepth,
                  view.storageQueuePeak,
                  (unsigned long)view.storageDroppedSnapshots);
    } else if (view.recording) {
      gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
      gfx->printf("Log:%lu Q:%u/%u", (unsigned long)view.logRecordCount,
                  view.storageQueueDepth, view.storageQueuePeak);
    } else {
      gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
      gfx->printf("Q:%u/%u Stack:%u", view.storageQueueDepth,
                  view.storageQueuePeak,
                  (unsigned int)view.storageTaskStackHighWater);
    }
  } else if (view.sdState == UiSdState::Error) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->printf(" %.12s", view.sdError);
  }

  gfx->drawLine(kContentLeft, 263, kContentRight, 263, RGB565_DARKGREY);
  gfx->setCursor(5, 267);
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->println("Click Header To Return");

  gfx->fillRect(kContentLeft, 280, kWideAreaWidth, 35, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setCursor(centeredTextX("TOOLS MENU", 1), 293);
  gfx->print("TOOLS MENU");
}

void UiManager::drawConfigUpdatePage(const ConfigUpdateView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("CONFIG UPLOAD");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 55);
  gfx->print("USB protocol: ESPCFG1");
  gfx->setCursor(5, 75);
  gfx->print("Status:");
  gfx->setCursor(5, 92);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print(view.statusText);

  if (view.expectedLength > 0) {
    gfx->setCursor(5, 120);
    gfx->printf("Bytes: %lu / %lu",
                (unsigned long)view.receivedLength,
                (unsigned long)view.expectedLength);
  }

  if (view.state == UiConfigState::Failed) {
    gfx->setCursor(5, 150);
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->printf("%.26s", view.errorText);
    gfx->setCursor(5, 170);
    gfx->print("Tap header to exit");
  } else if (view.state == UiConfigState::Success) {
    gfx->setCursor(5, 150);
    gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
    gfx->print("ACK sent");
    gfx->setCursor(5, 170);
    gfx->print("Loading new config...");
  } else {
    gfx->setCursor(5, 150);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->print("Run PC upload tool");
  }

  gfx->drawLine(kContentLeft, 265, kContentRight, 265, RGB565_DARKGREY);
  gfx->setCursor(centeredTextX("Tap header = cancel", 1), 285);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->print("Tap header = cancel");
  gfx->flush();
}

void UiManager::drawLogExportPage(const LogExportView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("LOG EXPORT");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 50);
  gfx->print("USB protocol: ESPLOG1");
  gfx->setCursor(5, 68);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print("Status:");
  gfx->setCursor(55, 68);
  gfx->print(view.statusText);

  if (view.state == UiLogExportState::Failed) {
    gfx->setCursor(5, 90);
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->print("SD not ready");
    gfx->setCursor(5, 106);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->print("Insert card & restart");
  } else if (view.state == UiLogExportState::WaitingCommand) {
    gfx->setCursor(5, 90);
    gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
    gfx->print("Ready for PC commands");
  }

  if (view.currentFile != nullptr && view.currentFile[0] != '\0') {
    gfx->setCursor(5, 110);
    gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
    gfx->print("File:");
    gfx->setCursor(5, 124);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    size_t fileLength = strlen(view.currentFile);
    const char *fileToShow = view.currentFile;
    if (fileLength > 24) fileToShow = view.currentFile + fileLength - 24;
    gfx->print(fileToShow);

    gfx->setCursor(5, 145);
    gfx->printf("%lu / %lu bytes",
                (unsigned long)view.transferOffset,
                (unsigned long)view.transferTotal);
    if (view.transferTotal > 0) {
      uint8_t percent =
          (uint8_t)(view.transferOffset * 100 / view.transferTotal);
      gfx->setCursor(5, 160);
      gfx->printf("%u%%", percent);
      gfx->drawRect(kContentLeft, 175, kButtonWidth, 12, RGB565_WHITE);
      gfx->fillRect(kContentLeft + 1, 176,
                    (uint16_t)(percent * kProgressInnerWidth / 100), 10,
                    RGB565_GREEN);
    }
  }

  if (view.errorText != nullptr && view.errorText[0] != '\0') {
    gfx->setCursor(5, 200);
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->print("Err: ");
    gfx->print(view.errorText);
  }

  gfx->drawLine(kContentLeft, 250, kContentRight, 250, RGB565_DARKGREY);
  gfx->setCursor(15, 265);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->print("Tap header to exit");
  gfx->setCursor(15, 285);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->print("Run PC exporter tool");
  gfx->flush();
}

void UiManager::drawTimeSyncPage(const TimeSyncView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("TIME SYNC");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 55);
  gfx->print("USB protocol: ESPTIME1");

  gfx->setCursor(5, 82);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print("Status:");
  gfx->setCursor(55, 82);
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->print(view.statusText);

  gfx->setCursor(5, 115);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->print("RTC:");
  gfx->setCursor(5, 135);
  gfx->setTextColor(view.rtcValid ? RGB565_WHITE : RGB565_ORANGE,
                    RGB565_BLACK);
  if (view.rtcValid) {
    gfx->print(view.rtcDateTimeText);
    gfx->setCursor(5, 153);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->printf("UTC+08:00 Source:%s", view.timeSourceText);
  } else {
    gfx->print("Waiting for PC time");
  }

  gfx->setCursor(5, 174);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("NTP: %s", view.ntpStatusText);
  if (view.maskedSsid != nullptr && view.maskedSsid[0] != '\0') {
    gfx->setCursor(5, 190);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->printf("WiFi: %s", view.maskedSsid);
  }

  if (view.errorText != nullptr && view.errorText[0] != '\0') {
    gfx->setCursor(5, 207);
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->print("Err: ");
    gfx->print(view.errorText);
    gfx->setCursor(5, 224);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->print("Fix PC request and retry");
  }

  gfx->drawLine(kContentLeft, 250, kContentRight, 250, RGB565_DARKGREY);
  gfx->setCursor(centeredTextX("Tap header to exit", 1), 268);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->print("Tap header to exit");
  gfx->setCursor(centeredTextX("Run tools/sync_rtc.py", 1), 290);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->print("Run tools/sync_rtc.py");
  gfx->flush();
}

void UiManager::drawSensorInfoPage(const SensorInfoView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("SENSOR INFO");
  gfx->drawLine(kContentLeft, 38, kContentRight, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(5, 52);
  gfx->printf("Address: %u", view.slaveAddress);

  gfx->setCursor(5, 72);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->print("Product Number:");
  gfx->setCursor(5, 86);
  gfx->setTextSize(2);
  gfx->printf("%lu", (unsigned long)view.productNum);

  gfx->setTextSize(1);
  gfx->setCursor(5, 110);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->print("Device Name:");
  gfx->setCursor(5, 126);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print(view.deviceName);

  drawSensorParameterArea(view);
  gfx->flush();
}

void UiManager::drawSensorParameterArea(const SensorInfoView &view) {
  gfx->fillRect(0, 148, BoardConfig::kDisplayWidth, 172, RGB565_BLACK);
  gfx->drawLine(kContentLeft, 148, kContentRight, 148, RGB565_DARKGREY);
  gfx->setTextSize(1);

  if (!view.supported) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->setCursor(5, 165);
    gfx->print("Unsupported PN");
    gfx->setCursor(5, 185);
    gfx->print("No parameter profile");
  } else {
#if defined(PYXIS_BOARD_WAVESHARE_28)
    // Keep the original left-to-right "label: value" reading order on the
    // wider 2.8-inch panel. Labels remain compact so long JSON field names fit,
    // while the live value uses double-size white text for better visibility.
    constexpr int16_t kFirstRowY = 157;
    constexpr int16_t kRowHeight = 27;
    constexpr int16_t kValueX = 126;
    for (uint8_t row = 0; row < view.rowCount; ++row) {
      const SensorParameterView &parameter = view.rows[row];
      const int16_t rowY = kFirstRowY + row * kRowHeight;

      gfx->setTextSize(1);
      gfx->setCursor(5, rowY + 4);
      gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
      gfx->printf("%s:", parameter.label);

      gfx->setTextSize(2);
      gfx->setCursor(kValueX, rowY);
      if (!parameter.valid) {
        gfx->setTextColor(RGB565_RED, RGB565_BLACK);
        gfx->print("--");
      } else {
        gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
        switch (parameter.decimals) {
          case 0: gfx->printf("%.0f", parameter.value); break;
          case 1: gfx->printf("%.1f", parameter.value); break;
          case 2: gfx->printf("%.2f", parameter.value); break;
          default: gfx->printf("%.3f", parameter.value); break;
        }
      }
    }
#else
    for (uint8_t row = 0; row < view.rowCount; ++row) {
      const SensorParameterView &parameter = view.rows[row];
      gfx->setCursor(5, 162 + row * 25);
      gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
      gfx->printf("%s: ", parameter.label);
      if (!parameter.valid) {
        gfx->setTextColor(RGB565_RED, RGB565_BLACK);
        gfx->print("--");
      } else {
        gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
        switch (parameter.decimals) {
          case 0: gfx->printf("%.0f", parameter.value); break;
          case 1: gfx->printf("%.1f", parameter.value); break;
          case 2: gfx->printf("%.2f", parameter.value); break;
          default: gfx->printf("%.3f", parameter.value); break;
        }
      }
    }
#endif
  }

  gfx->setTextSize(1);
  gfx->drawLine(kContentLeft, 270, kContentRight, 270, RGB565_DARKGREY);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(12, 282);
  if (view.pageCount > 1) {
    gfx->printf("Page %u/%u - tap bottom", view.pageNumber, view.pageCount);
  } else {
    gfx->print("No more parameters");
  }
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(centeredTextX("Header = back", 1), 302);
  gfx->print("Header = back");
}

void UiManager::drawDashboardDynamic(const DashboardView &view) {
  drawModeStatus(view.mode);

  if (view.rtcSupported) {
    constexpr int16_t kClockWidth = 52;
    int16_t clockX = BoardConfig::kDisplayWidth - kClockWidth - 3;
    gfx->fillRect(clockX, 12, kClockWidth, 12, RGB565_BLACK);
    gfx->setTextSize(1);
    gfx->setTextColor(view.rtcValid ? RGB565_CYAN : RGB565_DARKGREY,
                      RGB565_BLACK);
    gfx->setCursor(clockX + 2, 15);
    gfx->print(view.rtcTimeText);
  }

  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 104);
  gfx->printf("CH1:%4.1fmA ", view.channel1Ma);
  gfx->setCursor(5, 138);
  gfx->printf("CH2:%4.1fmA ", view.channel2Ma);

  gfx->setTextSize(1);
  gfx->setCursor(5, 202);
  if (view.mode == UiOperatingMode::Pass) {
    gfx->setTextColor(DARKGREY, RGB565_BLACK);
    gfx->println("Link: USB PassThrough");
    gfx->setCursor(5, 217);
    gfx->println("Master is computer.   ");
    gfx->flush();
    return;
  }

  gfx->fillRect(kContentLeft, 200, kWideAreaWidth, 115, RGB565_BLACK);
  if (view.mode == UiOperatingMode::UpgradePass) {
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
    gfx->setCursor(5, 202);
    gfx->print("UART:9600,8N1 RAW READY");
    gfx->setCursor(5, 222);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->printf("TX:%lu RX:%lu",
                static_cast<unsigned long>(view.rawUsbToUartBytes),
                static_cast<unsigned long>(view.rawUartToUsbBytes));
    gfx->setCursor(5, 242);
    gfx->setTextColor(
        (view.rawUsbToUartDrops == 0 && view.rawUartToUsbDrops == 0)
            ? RGB565_GREEN
            : RGB565_RED,
        RGB565_BLACK);
    gfx->printf("DROP:%lu/%lu",
                static_cast<unsigned long>(view.rawUsbToUartDrops),
                static_cast<unsigned long>(view.rawUartToUsbDrops));
    gfx->setCursor(5, 262);
    gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
    gfx->printf("BUF PEAK:%u/%u", view.rawRequestPeak, view.rawResponsePeak);
    gfx->setCursor(centeredTextX("Tap BOOT PASS to exit", 1), 300);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->print("Tap BOOT PASS to exit");
    gfx->flush();
    return;
  }

  if (view.mode == UiOperatingMode::Precheck) {
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
    gfx->print("Slave:15  FC03  46037-46040");

    gfx->setCursor(5, 221);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->print("s470Raw:");
    gfx->setCursor(70, 218);
    gfx->setTextSize(2);
    if (view.s470Valid) {
      gfx->printf("%.2f", view.s470Raw);
    } else {
      gfx->print("--");
    }

    gfx->setCursor(5, 251);
    gfx->setTextSize(1);
    gfx->print("t365Raw:");
    gfx->setCursor(70, 248);
    gfx->setTextSize(2);
    if (view.t365Valid) {
      gfx->printf("%.2f", view.t365Raw);
    } else {
      gfx->print("--");
    }

    gfx->setTextSize(1);
    gfx->setCursor(5, 279);
    if (view.precheckError == 0) {
      gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
      gfx->printf("Scan:%lu  OK",
                  (unsigned long)view.precheckScanSequence);
    } else if (view.precheckScanSequence == 0 &&
               view.precheckError == 0xFF) {
      gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
      gfx->print("Waiting for first reading...");
    } else {
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->printf("Scan:%lu  Modbus:0x%02X",
                  (unsigned long)view.precheckScanSequence,
                  view.precheckError);
    }
    gfx->setCursor(centeredTextX("Tap PRECHECK to switch", 1), 300);
    gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
    gfx->print("Tap PRECHECK to switch");
    gfx->flush();
    return;
  }

  switch (view.discoveryState) {
    case UiDiscoveryState::Idle:
      gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
      gfx->print("Discovery idle");
      break;

    case UiDiscoveryState::DiscoveringAddress:
      gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
      gfx->print("Discovering address...");
      gfx->setCursor(5, 217);
      gfx->printf("Try: %u/%u", view.attempt, view.maxAttempts);
      gfx->setCursor(5, 232);
      gfx->print("TX: 00 03 A028 0001");
      break;

    case UiDiscoveryState::DiscoveringSpecialAddress:
      gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
      gfx->print("Special discovery...");
      gfx->setCursor(5, 217);
      gfx->printf("Try: %u/%u", view.attempt, view.maxAttempts);
      gfx->setCursor(5, 232);
      gfx->print("TX: 00 67 ... 21 E6");
      break;

    case UiDiscoveryState::ReadingIdentity:
      gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
      gfx->print("Reading identity...");
      gfx->setCursor(5, 217);
      gfx->printf("Addr: %u  Try: %u/%u", view.slaveAddress,
                  view.attempt, view.maxAttempts);
      gfx->setCursor(5, 232);
      gfx->print("Regs: 41001-41014");
      break;

    case UiDiscoveryState::ReadingProductNumber:
      gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
      gfx->print("Reading PN only...");
      gfx->setCursor(5, 217);
      gfx->printf("Addr: %u  Try: %u/%u", view.slaveAddress,
                  view.attempt, view.maxAttempts);
      gfx->setCursor(5, 232);
      gfx->print("Regs: 41005-41006");
      break;

    case UiDiscoveryState::Success:
      gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
      gfx->printf("Sensor found  Addr:%u", view.slaveAddress);
      gfx->setCursor(5, 220);
      gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
      gfx->print("Name: ");
      gfx->print(view.deviceName);
      gfx->setCursor(5, 237);
      gfx->printf("PN: %lu", (unsigned long)view.productNum);
      gfx->fillRect(kContentLeft, 286, kWideAreaWidth, 29, RGB565_BLUE);
      gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
      gfx->setCursor(centeredTextX("Tap for details >", 1), 296);
      gfx->print("Tap for details >");
      break;

    case UiDiscoveryState::Failed:
      gfx->setTextColor(RGB565_RED, RGB565_BLACK);
      gfx->print("Discovery failed");
      gfx->setCursor(5, 217);
      gfx->print(view.errorText);
      if (view.exceptionResponse) {
        gfx->setCursor(5, 232);
        gfx->printf("Exception: 0x%02X", view.exceptionCode);
      }
      break;
  }
  gfx->flush();
}
