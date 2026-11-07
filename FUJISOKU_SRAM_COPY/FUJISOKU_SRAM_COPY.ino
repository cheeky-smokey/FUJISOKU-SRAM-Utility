// FUJISOKU SRAM Utility v2
//
// Silver Reed FUJISOKU SRAM カード (32KB) を、SPI SRAM / SD カード / PC (Web アプリ) と
// コピー・比較する Arduino UNO R4 用スケッチ。
//
// ファイル構成 (Arduino IDE ではタブとして開く)
//   FUJISOKU_SRAM_COPY.ino  このファイル。ライブラリ、グローバル変数、setup / loop
//   config.h                オプション部品 (USE_SD / USE_LCD / USE_SRAM62256)、設定値、ピン、レジスタ、制御ビット
//   hardware.ino            MCP23S17 / FUJISOKU カード / 62256 / SPI SRAM の 1 バイト読み書き
//   storage.ino             SD カードの状態管理 (抜き差し対応、USE_SD のときだけ)
//   operations.ino          コピー・比較・SD 保存/書き込み/照合などの処理
//   protocol.ino            PC との通信 (1 行コマンド)。Web アプリはこれを使う
//   lcd_ui.ino              表示 (20x4): 待機・進捗バー・結果。LCD (オプション) と Web アプリに出す
//
// 詳しくは docs/ を参照。

#include "config.h"  // オプション部品の選択はここ
#include "SPI.h"
#if USE_SD
#include <SdFat.h>  // ライブラリマネージャの "SdFat" (Bill Greiman)
#endif
#if USE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // ライブラリマネージャの "LiquidCrystal I2C" (Frank de Brabander)
#endif

#if USE_LCD
// LCD (20 文字 x 4 行、I2C: SDA = A4, SCL = A5)
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
#endif

// SD カード (オプション)
#if USE_SD
SdFat SD;
FsFile myFile;
#endif
uint8_t sd_state = SD_STATE_UNKNOWN;  // storage.ino が更新する (USE_SD = 0 なら UNKNOWN のまま)
uint32_t sd_checked_ms = 0;           // 最後に SD カードを確かめた時刻

// 処理の状態 (operations.ino / protocol.ino / lcd_ui.ino で共有)
char op_msg[48];             // 終了行の説明 (LCD の結果画面にも出す)
bool op_cancel = false;      // 中断要求 (PC からの '!')
uint8_t op_session = 0;      // 0: なし  1: 処理中  2: PC が LCD を操作中 (ui begin ～ ui end)
uint32_t op_session_ms = 0;  // 最後に動きがあった時刻 (PC 側が止まったら画面を戻すため)

void setup() {
  Serial.begin(115200);  // 接続待ちはしない (PC をつなぐ前から LCD に状態を出す)

  // SPI の /CS はすべて High (非選択) にしてから SPI を始める
  pinMode(PIN_CS_SD, OUTPUT);
  pinMode(PIN_CS_SPI_SRAM, OUTPUT);
  pinMode(PIN_CS_MCP, OUTPUT);
  digitalWrite(PIN_CS_SD, HIGH);
  digitalWrite(PIN_CS_SPI_SRAM, HIGH);
  digitalWrite(PIN_CS_MCP, HIGH);
  pinMode(PIN_MCP_INT, INPUT);
  SPI.begin();

  // MCP23S17 をリセットする (/RESET を 1 ms Low)
  pinMode(PIN_MCP_RESET, OUTPUT);
  digitalWrite(PIN_MCP_RESET, LOW);
  delay(1);
  digitalWrite(PIN_MCP_RESET, HIGH);
  delay(1);

  ui_begin();

  ui_splash_status("Bus init...");
  mcp_init();       // バスは待機状態 (カードも 62256 も非選択) になる
  spi_sram_init();  // SPI SRAM のモードを揃える

#if USE_SD
  // SD カードが無くても止まらない (無ければ SD:NONE と表示するだけ)
  ui_splash_status("Checking SD...");
  sd_mount();
#endif

  ui_show_idle();
  Serial.print("FUJISOKU SRAM Utility " FSU_VERSION " ready (LCD ");
  Serial.print(ui_lcd_present() ? "20x4" : "none");
  Serial.print(", SD ");
  Serial.print(USE_SD ? "on" : "off");
  Serial.print(", 62256 ");
  Serial.print(USE_SRAM62256 ? "on" : "off");
  Serial.println("). Send '?' for help.");
}

void loop() {
  proto_poll();     // PC からのコマンド
  ui_poll();        // 表示の更新 (SD / USB の状態、結果画面から待機画面への切り替え)
#if USE_SD
  sd_idle_check();  // SD カードが抜かれていないかの確認 (1.5 秒ごと)
#endif
}
