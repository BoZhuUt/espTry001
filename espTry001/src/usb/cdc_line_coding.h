#pragma once

#include <stdint.h>

// USB CDC encodings, independent of Arduino so validation can run on the host.
struct CdcLineCoding {
  uint32_t baud = 9600;
  uint8_t stopBits = 0;  // CDC: 0 = one stop bit
  uint8_t parity = 2;    // CDC: 0 = none, 1 = odd, 2 = even
  uint8_t dataBits = 8;

  bool supported() const {
    return (baud == 9600 || baud == 19200 || baud == 115200) &&
           stopBits == 0 && parity <= 2 && dataBits == 8;
  }

  // Preserve the proven USB packet aggregation floor. USB arrival gaps are
  // not UART wire gaps; reducing this to 1.75ms can split host USB bursts.
  uint32_t frameGapUs() const {
    const uint32_t bits = 1 + dataBits + (parity != 0 ? 1 : 0) + 1;
    const uint32_t gap = baud ? (bits * 3500000UL + baud - 1) / baud : 5000;
    return gap > 5000 ? gap : 5000;
  }
};
