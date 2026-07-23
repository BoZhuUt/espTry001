#include "touch_driver.h"

#include <Wire.h>

#include "board_config.h"
#include "../esp_lcd_touch_axs5106l.h"

namespace BoardTouch {

bool begin(uint16_t rotation, uint16_t width, uint16_t height) {
  Wire.begin(BoardConfig::kTouchSda, BoardConfig::kTouchScl);
  bsp_touch_init(&Wire, BoardConfig::kTouchReset,
                 BoardConfig::kTouchInterrupt, rotation, width, height);
#if TOUCH_DEBUG_OUTPUT
  Serial.println("AXS5106L Touch BSP Initialized.");
#endif
  return true;
}

bool read(int &x, int &y) {
  touch_data_t touchData;
  bsp_touch_read();
  if (!bsp_touch_get_coordinates(&touchData)) return false;

  x = touchData.coords[0].x;
  y = touchData.coords[0].y;
  return true;
}

}  // namespace BoardTouch
