#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace Cst3530Touch {

bool begin(TwoWire &wire, int8_t resetPin, int8_t interruptPin);
bool read(TwoWire &wire, int &x, int &y);

}  // namespace Cst3530Touch
