// ホスト (PC) でスケッチをビルドするための Arduino 互換スタブ
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <fcntl.h>

#ifndef ARDUINO
#define ARDUINO 10819
#endif
#define PROGMEM
#define F(x) (x)
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
typedef uint8_t byte;
typedef bool boolean;

#include <string>
#define radians(deg) ((deg) * 0.017453292519943295)
#define degrees(rad) ((rad) * 57.29577951308232)
class __FlashStringHelper;
// Adafruit_GFX がシグネチャに使うだけの最小限の String
class String {
public:
  String() {}
  String(const char *s) : s_(s) {}
  size_t length() const { return s_.size(); }
  const char *c_str() const { return s_.c_str(); }
private:
  std::string s_;
};

#include "Print.h"

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
long map(long x, long in_min, long in_max, long out_min, long out_max);

class SerialClass : public Print {
public:
  void begin(unsigned long) {}
  size_t write(uint8_t b) override;
  size_t write(const uint8_t *buf, size_t n) override;
  using Print::write;
  int available();
  int read();
  int peek();
  void flush();
  operator bool();
};
extern SerialClass Serial;
