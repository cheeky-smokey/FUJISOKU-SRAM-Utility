#pragma once
// LiquidCrystal_I2C (Frank de Brabander 版) のうちスケッチが使う部分。
// 中身は HD44780 (20x4) のモデル: DDRAM のアドレス (行の並び 0x00/0x40/0x14/0x54)、
// CGRAM、createChar の後にアドレスが CGRAM に残る挙動まで再現する。
#include "Arduino.h"
#include "Wire.h"
#define LCD_5x8DOTS 0x00

class LiquidCrystal_I2C : public Print {
public:
  LiquidCrystal_I2C(uint8_t addr, uint8_t cols, uint8_t rows) : addr_(addr), cols_(cols), rows_(rows) {}
  void init();
  void begin(uint8_t cols, uint8_t rows, uint8_t charsize = LCD_5x8DOTS);
  void clear();
  void home();
  void backlight();
  void noBacklight();
  void setCursor(uint8_t col, uint8_t row);
  void createChar(uint8_t location, uint8_t charmap[]);  // 本物と同じく const ではない
  size_t write(uint8_t c) override;
  using Print::write;

private:
  uint8_t addr_, cols_, rows_;
};
