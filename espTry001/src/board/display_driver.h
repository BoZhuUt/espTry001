#pragma once

#include <Arduino_GFX_Library.h>

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
