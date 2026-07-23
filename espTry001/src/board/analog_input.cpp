#include "analog_input.h"

#include "board_config.h"

namespace {

float readCalibratedMilliVolts(uint8_t pin) {
  uint32_t sum = 0;
  uint32_t minimum = UINT32_MAX;
  uint32_t maximum = 0;

  for (uint8_t i = 0; i < BoardConfig::kAdcSampleCount; i++) {
    uint32_t millivolts = analogReadMilliVolts(pin);
    sum += millivolts;
    if (millivolts < minimum) minimum = millivolts;
    if (millivolts > maximum) maximum = millivolts;
  }

  return (float)(sum - minimum - maximum) /
         (BoardConfig::kAdcSampleCount - 2);
}

}  // namespace

namespace BoardAnalog {

void begin() {
  pinMode(BoardConfig::kAnalogChannel1, INPUT);
  pinMode(BoardConfig::kAnalogChannel2, INPUT);
  analogSetAttenuation(ADC_11db);
}

void sample(Reading &channel1, Reading &channel2) {
  channel1.raw = analogRead(BoardConfig::kAnalogChannel1);
  channel2.raw = analogRead(BoardConfig::kAnalogChannel2);

  channel1.milliAmps =
      readCalibratedMilliVolts(BoardConfig::kAnalogChannel1) /
      BoardConfig::kShuntResistorOhms;
  channel2.milliAmps =
      readCalibratedMilliVolts(BoardConfig::kAnalogChannel2) /
      BoardConfig::kShuntResistorOhms;
}

}  // namespace BoardAnalog
