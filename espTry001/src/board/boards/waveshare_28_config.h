#pragma once

#include <Arduino.h>

// Hardware definition for ESP32-S3-Touch-LCD-2.8 V2 (CST3530).
namespace BoardConfig {

constexpr const char *kBoardName = "Waveshare ESP32-S3 Touch LCD 2.8 V2";

constexpr int16_t kDisplayWidth = 240;
constexpr int16_t kDisplayHeight = 320;
// Portrait is the bring-up orientation. It will be verified on real hardware.
constexpr uint8_t kDisplayRotation = 0;
constexpr int8_t kDisplayDc = 41;
constexpr int8_t kDisplayCs = 42;
constexpr int8_t kDisplaySclk = 40;
constexpr int8_t kDisplayMosi = 45;
constexpr int8_t kDisplayReset = 39;
constexpr int8_t kDisplayBacklight = 5;

constexpr int8_t kTouchSda = 1;
constexpr int8_t kTouchScl = 3;
constexpr int8_t kTouchReset = 2;
constexpr int8_t kTouchInterrupt = 4;

// The external THVD1406 board uses the Waveshare UART header.
constexpr int8_t kRs485Rx = 44;
constexpr int8_t kRs485Tx = 43;
constexpr uint32_t kRs485Baudrate = 9600;

// INA193: 5-ohm sense resistor * 20 V/V = 100 V/A.
constexpr int8_t kAnalogChannel1 = 15;
constexpr int8_t kAnalogChannel2 = 18;
constexpr uint8_t kAdcSampleCount = 16;
constexpr float kShuntResistorOhms = 100.0f;

constexpr bool kSdOneBit = true;
constexpr bool kSdD3MustBeHeldHigh = true;
constexpr int8_t kSdClk = 14;
constexpr int8_t kSdCmd = 17;
constexpr int8_t kSdD0 = 16;
constexpr int8_t kSdD1 = -1;
constexpr int8_t kSdD2 = -1;
constexpr int8_t kSdD3 = 21;

constexpr bool kHasPowerHold = true;
constexpr int8_t kPowerKey = 6;
constexpr int8_t kPowerHold = 7;

}  // namespace BoardConfig
