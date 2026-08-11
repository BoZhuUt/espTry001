#pragma once

#include <Arduino_GFX_Library.h>

#if defined(PYXIS_BOARD_WAVESHARE_28)
// Arduino Core 3.3.x and Arduino_GFX 1.6.x exchange RGB565 pixel words in
// opposite byte order on this SPI panel. Keep UI code in normal RGB space and
// convert once at the display boundary so every text, line and card is fixed.
constexpr uint16_t toDisplayRgb565(uint16_t color) {
  return static_cast<uint16_t>((color << 8) | (color >> 8));
}

#undef RGB565
#define RGB565(r, g, b)                                                     \
  toDisplayRgb565(static_cast<uint16_t>((((r) & 0xF8) << 8) |              \
                                         (((g) & 0xFC) << 3) | ((b) >> 3)))
#else
constexpr uint16_t toDisplayRgb565(uint16_t color) { return color; }
#endif

// Shared display object used by the current hand-drawn UI. Only the UI/loop
// execution context may call it. This ownership rule also applies after LVGL.
extern Arduino_GFX *gfx;

// Drives the backlight off as the first setup action, before the LCD controller
// and its frame memory are initialized.
void prepareBoardDisplay();

// Initializes the selected board's ST7789, vendor register sequence, rotation
// and a black frame. The backlight remains off until explicitly enabled.
// Returns false when the Arduino_GFX driver cannot start.
bool beginBoardDisplay();

void setBoardDisplayBacklight(bool enabled);
