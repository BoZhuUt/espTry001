#include "usb/usb_cdc_stream.h"
#include <algorithm>
#include <assert.h>
#include <deque>
#include <stdio.h>
#include <vector>

static bool ready = true, blocked = false;
static uint32_t ticks = 0;
static size_t disconnectAfter = 0;
static std::deque<uint8_t> fifo;
static std::vector<uint8_t> wire;
static constexpr size_t capacity = 17;
uint32_t millis() { return ticks; }
void delay(uint32_t ms) { ticks += ms; }
bool tud_cdc_n_ready(uint8_t) { return ready; }
uint32_t tud_cdc_n_write_available(uint8_t) { return capacity - fifo.size(); }
uint32_t tud_cdc_n_write(uint8_t, const void *data, uint32_t count) {
  // Real TinyUSB is allowed to overwrite its FIFO with DTR off. The adapter
  // must cap requests itself; this assertion exposes any silent overwrite.
  assert(count <= capacity - fifo.size());
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (uint32_t i = 0; i < count; ++i) fifo.push_back(bytes[i]);
  return count;
}
uint32_t tud_cdc_n_write_flush(uint8_t) {
  if (blocked) return 0;
  size_t count = std::min(fifo.size(), size_t(8));
  for (size_t i = 0; i < count; ++i) {
    wire.push_back(fifo.front());
    fifo.pop_front();
  }
  if (disconnectAfter && wire.size() >= disconnectAfter) ready = false;
  return count;
}
class FakeRx : public Stream {
 public:
  int available() override { return remaining; }
  int read() override { if (!remaining) return -1; --remaining; return 0xA5; }
  int peek() override { return remaining ? 0xA5 : -1; }
  size_t write(uint8_t) override { assert(false); return 0; }
  size_t write(const uint8_t *, size_t) override { assert(false); return 0; }
  void flush() override { assert(false); }
  int remaining = 3;
};
static void reset() {
  ready = true; blocked = false; ticks = 0; disconnectAfter = 0;
  fifo.clear(); wire.clear();
}
int main() {
  FakeRx rx;
  UsbCdcStream stream(rx);
  std::vector<uint8_t> payload(1029);
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = i & 255;
  assert(stream.available() == 3 && stream.peek() == 0xA5);
  assert(stream.read() == 0xA5 && stream.available() == 2);
  rx.remaining = -1;
  assert(stream.available() == 0);
  reset();
  assert(stream.write(payload.data(), payload.size()) == payload.size());
  while (!fifo.empty()) stream.flush();
  assert(wire == payload);
  reset(); blocked = true;
  size_t accepted = stream.write(payload.data(), payload.size());
  assert(accepted == capacity && ticks >= 250 && fifo.size() == capacity);
  blocked = false;
  while (!fifo.empty()) stream.flush();
  assert(wire == std::vector<uint8_t>(payload.begin(), payload.begin()+accepted));
  // Retrying a short write must preserve ordering without duplicating bytes.
  assert(stream.write(payload.data()+accepted, payload.size()-accepted) == payload.size()-accepted);
  while (!fifo.empty()) stream.flush();
  assert(wire == payload);
  reset(); ready = false;
  assert(stream.availableForWrite() == 0);
  assert(stream.write(payload.data(), payload.size()) == 0);
  assert(ticks == 0);
  reset(); disconnectAfter = 24;
  accepted = stream.write(payload.data(), payload.size());
  assert(accepted > 0 && accepted < payload.size());
  assert(wire == std::vector<uint8_t>(payload.begin(), payload.begin()+wire.size()));
  assert(stream.write(nullptr, 3) == 0 && stream.write(payload.data(), 0) == 0);
  puts("PASS: CDC TX without DTR gate, 1029-byte ordering, backpressure, short-write retry and disconnect");
}
