#pragma once
#include "Arduino.h"

class SPISettings {
public:
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) : clk(clock), order(bitOrder), mode(dataMode) {}
  SPISettings() : clk(1000000), order(MSBFIRST), mode(SPI_MODE0) {}
  bool operator==(const SPISettings &o) const { return clk == o.clk && order == o.order && mode == o.mode; }
  bool operator!=(const SPISettings &o) const { return !(*this == o); }
  uint32_t clk;
  uint8_t order, mode;
};

// UNO R4 (ArduinoSPI) と同じ振る舞い: endTransaction は何もしない、
// end() で止まり、beginTransaction は未初期化なら begin() を呼ぶ
class SPIClass {
public:
  void begin();
  void end();
  void beginTransaction(SPISettings s);
  void endTransaction();
  uint8_t transfer(uint8_t b);
  void transfer(void *buf, size_t n);
  uint16_t transfer16(uint16_t d);
};
extern SPIClass SPI;
