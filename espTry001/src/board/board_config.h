#pragma once

#include <Arduino.h>

// Hardware definition for the existing Waveshare ESP32-S3 1.47 platform.
// Business modules must not duplicate these pin assignments.
namespace BoardConfig {

constexpr int16_t kDisplayWidth = 172;
constexpr int16_t kDisplayHeight = 320;
constexpr uint8_t kDisplayRotation = 2;
constexpr int8_t kDisplayDc = 45;
constexpr int8_t kDisplayCs = 21;
constexpr int8_t kDisplaySclk = 38;
constexpr int8_t kDisplayMosi = 39;
constexpr int8_t kDisplayReset = 40;
constexpr int8_t kDisplayBacklight = 46;

constexpr int8_t kTouchSda = 42;
constexpr int8_t kTouchScl = 41;
constexpr int8_t kTouchReset = 47;
constexpr int8_t kTouchInterrupt = 48;

// The current PCB has RO/R connected to GPIO43 and DI/T to GPIO44.
constexpr int8_t kRs485Rx = 43;
constexpr int8_t kRs485Tx = 44;
constexpr uint32_t kRs485Baudrate = 9600;

constexpr int8_t kAnalogChannel1 = 1;
constexpr int8_t kAnalogChannel2 = 2;
constexpr uint8_t kAdcSampleCount = 16;
constexpr float kShuntResistorOhms = 100.0f;

constexpr int8_t kSdClk = 16;
constexpr int8_t kSdCmd = 15;
constexpr int8_t kSdD0 = 17;
constexpr int8_t kSdD1 = 18;
constexpr int8_t kSdD2 = 13;
constexpr int8_t kSdD3 = 14;

}  // namespace BoardConfig

// Compile-time oscilloscope test. Kept as a macro because main.cpp uses #if.
#ifndef RS485_TX_TEST_MODE
#define RS485_TX_TEST_MODE 0
#endif
