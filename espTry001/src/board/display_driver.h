#pragma once

#include <Arduino_GFX_Library.h>

// Shared display object used by the current hand-drawn UI. Only the UI/loop
// execution context may call it. This ownership rule also applies after LVGL.
extern Arduino_GFX *gfx;

// Initializes the 1.47-inch ST7789, vendor register sequence, rotation and
// backlight. Returns false when the Arduino_GFX driver cannot start.
bool beginBoardDisplay();
