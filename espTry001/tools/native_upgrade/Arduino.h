#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cctype>
using std::min;
extern uint32_t testTime;
inline uint32_t millis() { return testTime; }
struct String {
  std::string s;
  String(const char *p) : s(p) {}
  void toUpperCase() { for (char &c : s) c = std::toupper(c); }
  bool endsWith(const char *p) const { auto n = strlen(p); return s.size() >= n && s.compare(s.size() - n, n, p) == 0; }
};
