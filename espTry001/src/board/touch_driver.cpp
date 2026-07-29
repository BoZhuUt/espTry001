#include "touch_driver.h"

#include <Wire.h>

#include "board_config.h"
#if defined(PYXIS_BOARD_WAVESHARE_147)
#include "../esp_lcd_touch_axs5106l.h"
#elif defined(PYXIS_BOARD_WAVESHARE_28)
#include "cst3530_touch.h"
#endif

namespace BoardTouch {

bool begin(uint16_t rotation, uint16_t width, uint16_t height) {
  Wire.begin(BoardConfig::kTouchSda, BoardConfig::kTouchScl);
#if defined(PYXIS_BOARD_WAVESHARE_147)
  bsp_touch_init(&Wire, BoardConfig::kTouchReset,
                 BoardConfig::kTouchInterrupt, rotation, width, height);
#if TOUCH_DEBUG_OUTPUT
  Serial.println("AXS5106L Touch BSP Initialized.");
#endif
  return true;
#elif defined(PYXIS_BOARD_WAVESHARE_28)
  (void)rotation;
  (void)width;
  (void)height;
  bool online = Cst3530Touch::begin(
      Wire, BoardConfig::kTouchReset, BoardConfig::kTouchInterrupt);
#if TOUCH_DEBUG_OUTPUT
  Serial.println(online ? "CST3530 Touch Initialized."
                        : "CST3530 Touch Not Found.");
#endif
  return online;
#endif
}

bool read(int &x, int &y) {
#if defined(PYXIS_BOARD_WAVESHARE_147)
  touch_data_t touchData;
  bsp_touch_read();
  if (!bsp_touch_get_coordinates(&touchData)) return false;

  x = touchData.coords[0].x;
  y = touchData.coords[0].y;
  return true;
#elif defined(PYXIS_BOARD_WAVESHARE_28)
  if (!Cst3530Touch::read(Wire, x, y)) return false;
  return x >= 0 && y >= 0 &&
         x < BoardConfig::kDisplayWidth &&
         y < BoardConfig::kDisplayHeight;
#endif
}

}  // namespace BoardTouch
