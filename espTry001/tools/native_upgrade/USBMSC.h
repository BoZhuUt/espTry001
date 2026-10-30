#pragma once
#include <cstdint>
class USBMSC {
 public:
  static int32_t (*read)(uint32_t, uint32_t, void *, uint32_t);
  static int32_t (*write)(uint32_t, uint32_t, uint8_t *, uint32_t);
  static bool (*stop)(uint8_t, bool, bool);
  void vendorID(const char *) {} void productID(const char *) {} void productRevision(const char *) {}
  void onRead(decltype(read) f) { read = f; } void onWrite(decltype(write) f) { write = f; }
  void onStartStop(decltype(stop) f) { stop = f; }
  bool begin(uint32_t, uint16_t) { return true; } void mediaPresent(bool) {}
};
