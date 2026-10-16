#include "usb/cdc_line_coding.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  CdcLineCoding defaults;
  assert(defaults.supported());
  assert(defaults.baud == 9600 && defaults.parity == 2);
  unsigned count = 0;
  for (uint32_t baud : {9600u, 19200u, 115200u}) {
    for (uint8_t parity = 0; parity <= 2; ++parity) {
      CdcLineCoding coding{baud, 0, parity, 8};
      assert(coding.supported());
      assert(coding.frameGapUs() >= 5000);
      for (uint8_t bits : {5, 6, 7, 9, 16}) {
        coding.dataBits = bits;
        assert(!coding.supported());
      }
      coding.dataBits = 8;
      for (uint8_t stop : {1, 2, 3, 255}) {
        coding.stopBits = stop;
        assert(!coding.supported());
      }
      ++count;
    }
  }
  for (uint32_t baud : {0u, 1200u, 38400u, 230400u, 0xffffffffu}) {
    CdcLineCoding coding{baud, 0, 2, 8};
    assert(!coding.supported());
  }
  for (uint8_t parity : {3, 4, 255}) {
    CdcLineCoding coding{9600, 0, parity, 8};
    assert(!coding.supported());
  }
  puts("PASS: 9 CDC formats, invalid settings, USB aggregation floor");
  assert(count == 9);
}
