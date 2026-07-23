#pragma once

#include <Arduino.h>

namespace BoardTouch {

// Initializes the AXS5106L using the current display geometry and rotation.
bool begin(uint16_t rotation, uint16_t width, uint16_t height);

// Returns true for a pressed point and writes the first contact coordinate.
bool read(int &x, int &y);

}  // namespace BoardTouch
