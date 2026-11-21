#ifndef _SIM_ARDUINO_H
#define _SIM_ARDUINO_H
// 模拟器用 Arduino 兼容 shim（最小集）
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <algorithm>

typedef std::string String;

uint32_t millis();
void delay(uint32_t ms);

class SimSerial {
public:
  void begin(int) {}
  int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vprintf(fmt, ap); va_end(ap); return r;
  }
  void println(const char* s) { printf("%s\n", s); }
};
extern SimSerial Serial;

#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#endif
