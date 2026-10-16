#include "usb_cdc_stream.h"
#include <tusb.h>

int UsbCdcStream::available() {
  const int count = rx_.available();
  return count > 0 ? count : 0;
}
int UsbCdcStream::read() { return rx_.read(); }
int UsbCdcStream::peek() { return rx_.peek(); }

int UsbCdcStream::availableForWrite() {
  return tud_cdc_n_ready(0) ? tud_cdc_n_write_available(0) : 0;
}

void UsbCdcStream::flush() {
  if (tud_cdc_n_ready(0)) tud_cdc_n_write_flush(0);
}

size_t UsbCdcStream::write(uint8_t value) { return write(&value, 1); }

size_t UsbCdcStream::write(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) return 0;
  size_t accepted = 0;
  const uint32_t started = millis();
  while (accepted < length && tud_cdc_n_ready(0)) {
    const size_t space = tud_cdc_n_write_available(0);
    if (space > 0) {
      const size_t remaining = length - accepted;
      // TinyUSB may make its FIFO overwritable when DTR is low. Never write
      // beyond the reported free space: a single TX producer cannot overwrite
      // queued bytes, even when the host is slow or stops reading entirely.
      const size_t chunk = remaining < space ? remaining : space;
      accepted += tud_cdc_n_write(0, data + accepted, chunk);
    }
    tud_cdc_n_write_flush(0);
    if (accepted == length ||
        static_cast<uint32_t>(millis() - started) >= kWriteTimeoutMs) break;
    delay(1);
  }
  return accepted;
}
