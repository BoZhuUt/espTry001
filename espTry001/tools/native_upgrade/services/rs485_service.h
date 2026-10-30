#pragma once
#include <cstdint>
#include <vector>
#include <deque>
#include <algorithm>
class Rs485Service {
 public:
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  size_t limit = 37; // Deliberately force partial writes of each 1029-byte packet.
  bool enterUpgradePassAndWait() { tx.clear(); rx.clear(); return true; }
  size_t enqueueRawBytes(const uint8_t *p, size_t n) { n = std::min(n, limit); tx.insert(tx.end(), p, p + n); return n; }
  size_t readRawResponse(uint8_t *p, size_t) { if (rx.empty()) return 0; *p = rx.front(); rx.pop_front(); return 1; }
};
