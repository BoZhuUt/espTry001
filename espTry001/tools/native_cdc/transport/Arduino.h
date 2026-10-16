#pragma once
#include <stdint.h>
#include <stddef.h>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *, size_t) = 0;
  virtual int availableForWrite() { return 0; }
};
class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};
uint32_t millis();
void delay(uint32_t);
