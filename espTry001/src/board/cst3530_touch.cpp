#include "cst3530_touch.h"

namespace {

constexpr uint8_t kAddress = 0x58;
constexpr uint8_t kReadPointCommand[] = {0xD0, 0x07, 0x00, 0x00};
constexpr uint8_t kAcknowledgeCommand[] = {0xD0, 0x00, 0x02, 0xAB};
constexpr size_t kFirstPointPacketSize = 9;

int8_t touchInterruptPin = -1;
bool contactLatched = false;

bool writeCommand(TwoWire &wire, const uint8_t *command, size_t length) {
  wire.beginTransmission(kAddress);
  wire.write(command, length);
  return wire.endTransmission() == 0;
}

bool checksumValid(const uint8_t *packet) {
  uint16_t expected =
      static_cast<uint16_t>(packet[0]) |
      (static_cast<uint16_t>(packet[1]) << 8);
  uint16_t actual = 0x55;
  for (size_t index = 4; index < kFirstPointPacketSize; ++index) {
    actual += packet[index];
  }
  return actual == expected;
}

}  // namespace

namespace Cst3530Touch {

bool begin(TwoWire &wire, int8_t resetPin, int8_t interruptPin) {
  touchInterruptPin = interruptPin;
  contactLatched = false;
  pinMode(touchInterruptPin, INPUT_PULLUP);

  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, LOW);
  delay(8);
  digitalWrite(resetPin, HIGH);
  delay(50);

  // Probe the controller's normal-mode address without changing its state.
  wire.beginTransmission(kAddress);
  return wire.endTransmission() == 0;
}

bool read(TwoWire &wire, int &x, int &y) {
  // CST3530 automatically enters a low-power state. Reading only while INT is
  // asserted avoids spurious I2C errors and follows the vendor recommendation.
  if (digitalRead(touchInterruptPin) != LOW) return false;

  uint8_t packet[kFirstPointPacketSize] = {};
  if (!writeCommand(wire, kReadPointCommand, sizeof(kReadPointCommand))) {
    return false;
  }

  size_t received = wire.requestFrom(
      static_cast<uint8_t>(kAddress),
      static_cast<uint8_t>(kFirstPointPacketSize));
  if (received != kFirstPointPacketSize) {
    while (wire.available()) wire.read();
    writeCommand(wire, kAcknowledgeCommand, sizeof(kAcknowledgeCommand));
    return false;
  }
  for (size_t index = 0; index < kFirstPointPacketSize; ++index) {
    packet[index] = wire.read();
  }

  // Acknowledge releases and invalid packets too, otherwise INT can remain
  // asserted and stall subsequent reports.
  bool acknowledged =
      writeCommand(wire, kAcknowledgeCommand, sizeof(kAcknowledgeCommand));
  if (!acknowledged || !checksumValid(packet)) return false;

  uint8_t reportType = packet[2];
  uint8_t fingerCount = packet[3] & 0x0F;
  uint8_t event = packet[8] >> 4;
  if (reportType != 0xFF) return false;

  // One physical contact must produce at most one UiAction. Without this
  // latch, a held finger can open a page and immediately activate that page's
  // back region using a second down/move report.
  if (fingerCount == 0 || event == 0) {
    contactLatched = false;
    return false;
  }
  if (contactLatched) return false;
  contactLatched = true;

  x = packet[4] | ((packet[7] & 0x0F) << 8);
  y = packet[5] | ((packet[7] & 0xF0) << 4);
  return true;
}

}  // namespace Cst3530Touch
