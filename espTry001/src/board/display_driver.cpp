#include "display_driver.h"

#include "board_config.h"

namespace {

Arduino_DataBus *displayBus = new Arduino_ESP32SPI(
    BoardConfig::kDisplayDc, BoardConfig::kDisplayCs,
    BoardConfig::kDisplaySclk, BoardConfig::kDisplayMosi);

void applyVendorRegisterSequence() {
#if defined(PYXIS_BOARD_WAVESHARE_147)
  static const uint8_t initOperations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x60,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
#elif defined(PYXIS_BOARD_WAVESHARE_28)
  // Waveshare's ST7789 sequence for ESP32-S3-Touch-LCD-2.8.
  static const uint8_t initOperations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0xB0,
    WRITE_BYTES, 2,
    0x00, 0xE8,

    WRITE_COMMAND_8, 0xB2,
    WRITE_BYTES, 5,
    0x0C, 0x0C, 0x00, 0x33, 0x33,

    WRITE_C8_D8, 0xB7, 0x75,
    WRITE_C8_D8, 0xBB, 0x1A,
    WRITE_C8_D8, 0xC0, 0x2C,

    WRITE_COMMAND_8, 0xC2,
    WRITE_BYTES, 2,
    0x01, 0xFF,

    WRITE_C8_D8, 0xC3, 0x13,
    WRITE_C8_D8, 0xC4, 0x20,
    WRITE_C8_D8, 0xC6, 0x0F,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 2,
    0xA4, 0xA1,

    WRITE_C8_D8, 0xD6, 0xA1,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 14,
    0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38,
    0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 14,
    0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37,
    0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E,

    WRITE_COMMAND_8, 0x21,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
#endif

  displayBus->batchOperation(initOperations, sizeof(initOperations));
}

}  // namespace

#if defined(PYXIS_BOARD_WAVESHARE_147)
Arduino_GFX *gfx = new Arduino_ST7789(
    displayBus, BoardConfig::kDisplayReset, BoardConfig::kDisplayRotation,
    false, BoardConfig::kDisplayWidth, BoardConfig::kDisplayHeight,
    34, 0, 34, 0);
#elif defined(PYXIS_BOARD_WAVESHARE_28)
Arduino_GFX *gfx = new Arduino_ST7789(
    displayBus, BoardConfig::kDisplayReset, BoardConfig::kDisplayRotation,
    false, BoardConfig::kDisplayWidth, BoardConfig::kDisplayHeight,
    0, 0, 0, 0);
#endif

void prepareBoardDisplay() {
  pinMode(BoardConfig::kDisplayBacklight, OUTPUT);
  digitalWrite(BoardConfig::kDisplayBacklight, LOW);
}

bool beginBoardDisplay() {
  prepareBoardDisplay();
  if (!gfx->begin()) return false;

  applyVendorRegisterSequence();
  gfx->setRotation(BoardConfig::kDisplayRotation);
  gfx->fillScreen(RGB565_BLACK);
  return true;
}

void setBoardDisplayBacklight(bool enabled) {
  digitalWrite(BoardConfig::kDisplayBacklight, enabled ? HIGH : LOW);
}
