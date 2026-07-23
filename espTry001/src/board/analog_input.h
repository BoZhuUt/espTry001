#pragma once

#include <Arduino.h>

namespace BoardAnalog {

struct Reading {
  uint16_t raw;
  float milliAmps;
};

void begin();

// Samples both channels in the same order as the original main.cpp code.
void sample(Reading &channel1, Reading &channel2);

}  // namespace BoardAnalog
