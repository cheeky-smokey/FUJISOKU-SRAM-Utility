#pragma once
#include "Arduino.h"
// I2C。LCD のアドレスにだけ応答する (--no-lcd なら誰も応答しない)
class TwoWire {
public:
  void begin();
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t addr);
  uint8_t endTransmission(bool stop = true);
  size_t write(uint8_t b);
};
extern TwoWire Wire;
