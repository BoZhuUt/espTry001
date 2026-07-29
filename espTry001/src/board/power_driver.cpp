#include "power_driver.h"

#include <Arduino.h>

#include "board_config.h"

void beginBoardPower() {
  if (!BoardConfig::kHasPowerHold) return;

  pinMode(BoardConfig::kPowerKey, INPUT_PULLUP);
  pinMode(BoardConfig::kPowerHold, OUTPUT);
  digitalWrite(BoardConfig::kPowerHold, LOW);
  delay(100);
  if (digitalRead(BoardConfig::kPowerKey) == LOW) {
    digitalWrite(BoardConfig::kPowerHold, HIGH);
  }
}
