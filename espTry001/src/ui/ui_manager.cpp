#include "ui_manager.h"

#include "board/display_driver.h"

void UiManager::clearScreen() {
  gfx->fillScreen(RGB565_BLACK);
}

void UiManager::flush() {
  gfx->flush();
}

void UiManager::drawDashboardStatic(bool masterMode) {
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("ESP32-S3 485");
  gfx->drawLine(5, 38, 165, 38, RGB565_YELLOW);

  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(5, 50);
  gfx->printf("Mode: ");
  drawModeStatus(masterMode);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(5, 66);
  gfx->printf("485 Config: 9600,8E1\n");
  gfx->drawLine(5, 78, 165, 78, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 88);
  gfx->println("[ 4-20mA Sampling ]");
  gfx->drawLine(5, 175, 165, 175, RGB565_YELLOW);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(5, 185);
  gfx->println("[ Modbus RTU Data ]");

  if (!masterMode) drawPassModeHint();
}

void UiManager::drawModeStatus(bool masterMode) {
  gfx->fillRect(55, 40, 112, 25, RGB565_BLACK);
  gfx->setTextSize(2);
  if (masterMode) {
    gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
    gfx->setCursor(62, 44);
    gfx->print("MASTER");
  } else {
    gfx->setTextColor(RGB565_MAGENTA, RGB565_BLACK);
    gfx->setCursor(72, 44);
    gfx->print("PASS");
  }
}

void UiManager::drawPassModeHint() {
  gfx->fillRect(5, 286, 165, 29, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setTextSize(1);
  gfx->setCursor(28, 296);
  gfx->print("Tap PASS to switch");
}

void UiManager::clearTransferStatus(bool masterMode) {
  gfx->fillRect(5, 252, 165, 24, RGB565_BLACK);
  if (!masterMode) drawPassModeHint();
}

void UiManager::showTransferStatus(const char *text, uint16_t color) {
  gfx->fillRect(5, 252, 165, 20, color);
  gfx->setTextColor(RGB565_WHITE, color);
  gfx->setTextSize(1);
  gfx->setCursor(12, 258);
  gfx->print(text);
  gfx->flush();
}

void UiManager::drawToolsMenuPage() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("TOOLS MENU");
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->setCursor(5, 50);
  gfx->print("Tap header to go back");

  gfx->fillRect(5, 60, 162, 56, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setTextSize(2);
  gfx->setCursor(15, 80);
  gfx->print("1.CONFIG");
  gfx->setCursor(15, 98);
  gfx->print("  UPLOAD");

  gfx->fillRect(5, 130, 162, 56, RGB565_DARKGREEN);
  gfx->setTextColor(RGB565_WHITE, RGB565_DARKGREEN);
  gfx->setTextSize(2);
  gfx->setCursor(15, 150);
  gfx->print("2.LOG");
  gfx->setCursor(15, 168);
  gfx->print("  EXPORT");

  gfx->fillRect(5, 200, 162, 56, RGB565_DARKGREY);
  gfx->setTextColor(RGB565_WHITE, RGB565_DARKGREY);
  gfx->setTextSize(2);
  gfx->setCursor(55, 225);
  gfx->print("3.BACK");

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
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(5, 60);
  gfx->println("Author: Bo");
  gfx->setCursor(5, 100);
  gfx->println(view.firmwareVersion);
  gfx->setCursor(5, 130);
  gfx->println("esp32-utility");

  gfx->setTextSize(1);
  gfx->setCursor(5, 165);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("Profile: %s\n", view.profileSource);
  gfx->setCursor(5, 182);
  gfx->printf("Config: %s\n", view.configVersion);
  if (view.profileFallback) {
    gfx->setCursor(5, 199);
    gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
    gfx->printf("Fallback: %.18s", view.profileError);
  }

  gfx->setCursor(5, 216);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->printf("SD: %s", view.sdStateText);
  if (view.sdState == UiSdState::Ready) {
    gfx->printf(" %.0fMB free", (double)(view.sdFreeBytes / 1048576));
    if (view.recording) {
      gfx->setCursor(5, 232);
      gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
      gfx->printf("Log: %lu records", (unsigned long)view.logRecordCount);
    }
  } else if (view.sdState == UiSdState::Error) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->printf(" %.12s", view.sdError);
  }

  gfx->drawLine(5, 250, 165, 250, RGB565_DARKGREY);
  gfx->setCursor(5, 255);
  gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
  gfx->println("Click Header To Return");

  gfx->fillRect(5, 280, 165, 35, RGB565_BLUE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setCursor(35, 293);
  gfx->print("TOOLS MENU");
}

void UiManager::drawConfigUpdatePage(const ConfigUpdateView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(5, 15);
  gfx->print("CONFIG UPLOAD");
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

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

  gfx->drawLine(5, 265, 165, 265, RGB565_DARKGREY);
  gfx->setCursor(18, 285);
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
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

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
      gfx->drawRect(5, 175, 162, 12, RGB565_WHITE);
      gfx->fillRect(6, 176, (uint16_t)(percent * 160 / 100), 10,
                    RGB565_GREEN);
    }
  }

  if (view.errorText != nullptr && view.errorText[0] != '\0') {
    gfx->setCursor(5, 200);
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->print("Err: ");
    gfx->print(view.errorText);
  }

  gfx->drawLine(5, 250, 165, 250, RGB565_DARKGREY);
  gfx->setCursor(15, 265);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->print("Tap header to exit");
  gfx->setCursor(15, 285);
  gfx->setTextColor(RGB565_DARKGREY, RGB565_BLACK);
  gfx->print("Run PC exporter tool");
  gfx->flush();
}

void UiManager::drawSensorInfoPage(const SensorInfoView &view) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 15);
  gfx->println("SENSOR INFO");
  gfx->drawLine(5, 38, 165, 38, RGB565_GREEN);

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
  gfx->fillRect(0, 148, 172, 172, RGB565_BLACK);
  gfx->drawLine(5, 148, 165, 148, RGB565_DARKGREY);
  gfx->setTextSize(1);

  if (!view.supported) {
    gfx->setTextColor(RGB565_RED, RGB565_BLACK);
    gfx->setCursor(5, 165);
    gfx->print("Unsupported PN");
    gfx->setCursor(5, 185);
    gfx->print("No parameter profile");
  } else {
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
  }

  gfx->drawLine(5, 270, 165, 270, RGB565_DARKGREY);
  gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
  gfx->setCursor(12, 282);
  if (view.pageCount > 1) {
    gfx->printf("Page %u/%u - tap bottom", view.pageNumber, view.pageCount);
  } else {
    gfx->print("No more parameters");
  }
  gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
  gfx->setCursor(45, 302);
  gfx->print("Header = back");
}

void UiManager::drawDashboardDynamic(const DashboardView &view) {
  drawModeStatus(view.masterMode);

  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(5, 104);
  gfx->printf("CH1:%4.1fmA ", view.channel1Ma);
  gfx->setCursor(5, 138);
  gfx->printf("CH2:%4.1fmA ", view.channel2Ma);

  gfx->setTextSize(1);
  gfx->setCursor(5, 202);
  if (!view.masterMode) {
    gfx->setTextColor(DARKGREY, RGB565_BLACK);
    gfx->println("Link: USB PassThrough");
    gfx->setCursor(5, 217);
    gfx->println("Master is computer.   ");
    gfx->flush();
    return;
  }

  gfx->fillRect(5, 200, 165, 115, RGB565_BLACK);
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
      gfx->fillRect(5, 286, 165, 29, RGB565_BLUE);
      gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
      gfx->setCursor(28, 296);
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
