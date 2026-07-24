#pragma once

#include <Arduino.h>

// USB CDC 同时承载 Modbus RTU、ESPCFG1 和 ESPLOG1 二进制协议。
// 发布固件必须默认关闭普通文本日志，否则 Serial.print() 产生的字符
// 会被上位机当作协议数据。现场调试时可在 platformio.ini 的 build_flags
// 中临时加入 -DUSB_DEBUG_LOG=1，调试完成后应恢复为 0。
#ifndef USB_DEBUG_LOG
#define USB_DEBUG_LOG 0
#endif

#if USB_DEBUG_LOG
#define DEBUG_LOG_PRINT(...)   Serial.print(__VA_ARGS__)
#define DEBUG_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_LOG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
#define DEBUG_LOG_PRINT(...)   do { } while (0)
#define DEBUG_LOG_PRINTLN(...) do { } while (0)
#define DEBUG_LOG_PRINTF(...)  do { } while (0)
#endif
