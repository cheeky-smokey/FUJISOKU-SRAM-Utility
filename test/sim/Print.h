#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buf, size_t n) {
    size_t c = 0;
    while (n--) c += write(*buf++);
    return c;
  }
  size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = 10) { return printNum(v, base); }
  size_t print(int v, int base = 10) { return printSigned(v, base); }
  size_t print(unsigned int v, int base = 10) { return printNum(v, base); }
  size_t print(long v, int base = 10) { return printSigned(v, base); }
  size_t print(unsigned long v, int base = 10) { return printNum(v, base); }
  size_t println() { return write((const uint8_t *)"\r\n", 2); }
  size_t println(const char *s) { return print(s) + println(); }
  size_t println(char c) { return print(c) + println(); }
  size_t println(unsigned char v, int base = 10) { return print(v, base) + println(); }
  size_t println(int v, int base = 10) { return print(v, base) + println(); }
  size_t println(unsigned int v, int base = 10) { return print(v, base) + println(); }
  size_t println(long v, int base = 10) { return print(v, base) + println(); }
  size_t println(unsigned long v, int base = 10) { return print(v, base) + println(); }

private:
  size_t printNum(unsigned long v, int base) {
    char buf[40];
    if (base == 16) snprintf(buf, sizeof(buf), "%lX", v);
    else if (base == 8) snprintf(buf, sizeof(buf), "%lo", v);
    else if (base == 2) {
      int i = 0;
      char t[40];
      if (v == 0) t[i++] = '0';
      while (v) { t[i++] = '0' + (v & 1); v >>= 1; }
      int n = 0;
      while (i) buf[n++] = t[--i];
      buf[n] = 0;
    } else snprintf(buf, sizeof(buf), "%lu", v);
    return write(buf);
  }
  size_t printSigned(long v, int base) {
    if (base == 10) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%ld", v);
      return write(buf);
    }
    return printNum((unsigned long)v, base);
  }
};
