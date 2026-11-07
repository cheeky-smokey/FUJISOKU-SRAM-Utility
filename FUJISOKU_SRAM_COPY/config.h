// FUJISOKU SRAM Utility - 設定・定数・共通定義
//
// つないだ部品に合わせて書き換えるのは、先頭の「オプション部品」だけ。
// それより下は配線と部品の仕様で決まる値 (docs/hardware.md と対応)。
#ifndef FSU_CONFIG_H
#define FSU_CONFIG_H

#include <Arduino.h>

#define FSU_VERSION "2.6"

// =====================================================================
// オプション部品 (1 = 使う、0 = 使わない)
//   必須: MCP23S17 x 2、FUJISOKU カードのスロット
//   カードの吸い出し・書き込み・比較は Web アプリ (PC) でできるので、ほかの部品はなくてもよい
// =====================================================================

// SD カード (本体だけでの保存・照合・書き戻し)。既定は 0 (カードの保存は Web アプリで PC にできる)。
//   1 にするには SdFat ライブラリが要る。起動時にカードが無くても、処理の合間に抜き差ししても動く。
//   0 のときは SdFat ライブラリは不要で、sd コマンドと B L V S W は ERR DISABLED を返す。
#ifndef USE_SD
#define USE_SD 0
#endif

// 20 文字 x 4 行のキャラクタ LCD (I2C)。
//   0 にすると LiquidCrystal I2C ライブラリも不要になる。
//   1 のままでも、起動時に LCD が見つからなければ表示を飛ばして動く (Web アプリには同じ内容が出る)。
#ifndef USE_LCD
#define USE_LCD 1
#endif

// SPI SRAM 23LC512 (本体だけでのコピー・比較の一時置き場、/CS = D6)。
//   1 のときは起動時に SPI SRAM があるかを確かめる。見つからなければ SPI SRAM を使う操作は ERR NOSPI を返す。
//   0 にすると SPI SRAM のコードが入らず、対象 s と R C X E S W T は ERR DISABLED を返す。
#ifndef USE_SPI_SRAM
#define USE_SPI_SRAM 1
#endif

// カードの検出 (カードの F37 /CARD DET → D2)。
//   1 にすると、カードが入っていないときはカードを使う操作が ERR NOCARD を返し、処理中に抜かれたら止める。
//   0 にすると、いつも入っているものとして扱う (D2 を配線していない基板)。
#ifndef USE_CARD_DET
#define USE_CARD_DET 1
#endif

// カードの電池電圧 (カードの F2 BATT-DET → A2)。
//   1 にすると、電圧を Web アプリと LCD に出し、BATT_LOW_MV 以下で警告する。
#ifndef USE_BATT_DET
#define USE_BATT_DET 1
#endif

// 動作確認用の並列 SRAM 62256 (カードと同じバスにつなぐ)。
//   1 にすると、コマンドの対象 p として読み書き・コピー・比較・バスの配線試験 (test p) に使える。
#ifndef USE_SRAM62256
#define USE_SRAM62256 0
#endif

// =====================================================================
// 設定
// =====================================================================

// LCD の I2C アドレス (PCF8574 バックパック)。作者の基板は 0x27。ほかのバックパックでは 0x3F のこともある
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif
#define LCD_COLS 20
#define LCD_ROWS 4

// 結果画面を表示しておく時間 [ms] (過ぎたら待機画面に戻る)
#ifndef RESULT_HOLD_MS
#define RESULT_HOLD_MS 15000
#endif

// MCP23S17 と SPI SRAM を操作するときの SPI クロック [Hz] (MCP23S17 の上限は 10 MHz)
#ifndef SPI_BUS_HZ
#define SPI_BUS_HZ 4000000
#endif

// /CARD DET の向き: 1 = カードが入ると Low (D2 は内蔵プルアップ)
#ifndef CARD_DET_ACTIVE_LOW
#define CARD_DET_ACTIVE_LOW 1
#endif

// カードの電池の警告電圧 [mV]。これ以下で警告する
#ifndef BATT_LOW_MV
#define BATT_LOW_MV 1000
#endif

// A2 の測定範囲 [mV] (UNO R4 は 5V が基準)。テスターで測った 5V の値にすると電圧が正確になる
#ifndef BATT_ADC_FULL_MV
#define BATT_ADC_FULL_MV 5000
#endif

// SD カードの生存確認の間隔 [ms] (カードが READY のときだけ行う)
#ifndef SD_ALIVE_CHECK_MS
#define SD_ALIVE_CHECK_MS 1500
#endif

// =====================================================================
// データ量・識別子
// =====================================================================
#define CARD_SIZE 0x8000UL       // 本体だけの処理 (コピー・比較・消去・SD) が扱う範囲 0x0000-0x7FFF (32 KB)
#define CARD_MAX_SIZE 0x40000UL  // PC からの rd / wr / fill でカードに使える範囲 (A0-A17 = 256 KB)
#define SPI_SRAM_SIZE 0x10000UL  // SPI SRAM 23LC512 (64 KB)
#define SRAM62256_SIZE 0x8000UL  // 62256 (32 KB)
#define CHUNK_BYTES 64           // PC とのやりとりの 1 行あたりのバイト数
#define BLOCK_BYTES 256          // SD / コピー処理の 1 ブロック
#define ERASE_VALUE 0xFF         // 消去で書く値

// バックアップ中に使う一時ファイル名 (成功したら本来の名前に付け替える)
#define SD_TMP_NAME "FSU_TMP.TMP"

// 転送対象 (dev 引数)。コマンドでは f / s / p
#define DEV_SPI_SRAM 0   // s: SPI SRAM (USE_SPI_SRAM のときだけ)
#define DEV_FUJISOKU 1   // f: FUJISOKU カード
#define DEV_SRAM62256 2  // p: 並列 SRAM 62256 (USE_SRAM62256 のときだけ)

// SD カードの状態
#define SD_STATE_UNKNOWN 0
#define SD_STATE_ABSENT 1
#define SD_STATE_READY 2

// 処理結果
#define RES_OK 0
#define RES_NG 1
#define RES_ERR 2
#define RES_CANCEL 3

// =====================================================================
// Arduino のピン
//   SPI: D13 SCK / D12 MISO / D11 MOSI (MCP23S17、SPI SRAM、SD カードで共有)
//   I2C: A4 SDA / A5 SCL (LCD)
//   D0、D1、D8、D9、D10、A1-A3 は未使用
// =====================================================================
#define PIN_CS_SD 7        // SD カードの /CS。USE_SD = 0 でも High にしておく (モジュールが付いたままでも邪魔しない)
#define PIN_CS_SPI_SRAM 6  // SPI SRAM の /CS。USE_SPI_SRAM = 0 でも High にしておく
#define PIN_MCP_RESET 5    // MCP23S17 の /RESET (2 個共通)
#define PIN_CS_MCP 4       // MCP23S17 の /CS (2 個共通。ハードウェアアドレスで区別する)
#define PIN_MCP_INT 3      // 未接続 (MCP23S17 の INT は使わない。入力にしておくだけ)
#define PIN_CARD_DET 2     // カードの /CARD DET (F37)。内蔵プルアップの入力
#define PIN_BATT_DET A2    // カードの BATT-DET (F2)。アナログ入力

// =====================================================================
// MCP23S17 (GPIO エキスパンダ)
// =====================================================================
// チャネル = ハードウェアアドレス (A2 A1 A0 ピンの値)
#define MCP_CH_ADDR 6  // ch6: GPA = A0-A7、GPB = A8-A15
#define MCP_CH_DATA 7  // ch7: GPA = D0-D7
#define MCP_CH_CTRL 7  // ch7: GPB = 制御線 (下の CTRL_*)

// SPI のオペコード: 0 1 0 0 A2 A1 A0 R/W
#define MCP_OP_WRITE(ch) (0x40 | ((ch) << 1))
#define MCP_OP_READ(ch) (0x41 | ((ch) << 1))

// レジスタアドレス (IOCON.BANK = 1 の配置)
#define MCP_IODIR_A 0x00  // 向き (1 = 入力、0 = 出力)
#define MCP_GPPU_A 0x06   // プルアップ (1 = 有効)
#define MCP_GPIO_A 0x09   // 読むとピンの状態、書くと出力ラッチ
#define MCP_OLAT_A 0x0A   // 出力ラッチ
#define MCP_IODIR_B 0x10
#define MCP_GPPU_B 0x16
#define MCP_GPIO_B 0x19
#define MCP_OLAT_B 0x1A
#define MCP_IOCON_BANK0 0x0A  // リセット直後 (BANK = 0) の IOCON のアドレス

// IOCON の設定値
#define IOCON_BANK 0x80  // レジスタをポート A / B ごとにまとめる (上の配置になる)
#define IOCON_HAEN 0x08  // ハードウェアアドレスを有効にする (同じ /CS の 2 個を区別する)
#define IOCON_ODR 0x04   // INT ピンをオープンドレインにする
#define MCP_IOCON_VALUE (IOCON_BANK | IOCON_HAEN | IOCON_ODR)  // 0x8C

#define MCP_ALL_INPUT 0xFF
#define MCP_ALL_OUTPUT 0x00

// =====================================================================
// ch7 GPB: カードと 62256 の制御線 (ビット = 1 で High)
// =====================================================================
//   B0  カードの CS2 (High で選択) と 62256 の /CS (Low で選択) を兼ねる
//   B1  カードの /OE
//   B2  62256 の /OE
//   B3  /WE (カードと 62256 で共通)
//   B4  カードの /CE1
//   B5  未使用 (どこにもつながない。High のまま)
//   B6  A16、B7 A17 (アドレスの上位。0x0000-0xFFFF では 0)
#define CTRL_CS2 0x01
#define CTRL_CARD_nOE 0x02
#define CTRL_62256_nOE 0x04
#define CTRL_nWE 0x08
#define CTRL_CARD_nCE1 0x10
#define CTRL_B5_UNUSED 0x20
#define CTRL_A16 0x40
#define CTRL_A17 0x80

// 状態ごとの値 (A16 / A17 は 0。アドレスを出すときに足す)
//   待機     0x3F: カードは /CE1 = H、62256 は /CS = H で、どちらも非選択
//   カード選択 0x2F: /CE1 = L、CS2 = H
//   カード解放 0x3E: /CE1 = H、CS2 = L (1 バイトごとの後始末。v1 と同じ)
//                    B0 を兼ねているので 62256 が選ばれた状態になるが、/OE も /WE も H なのでバスには出てこない
//   62256 選択 0x3E: /CS = L
#define CTRL_IDLE (CTRL_CS2 | CTRL_CARD_nOE | CTRL_62256_nOE | CTRL_nWE | CTRL_CARD_nCE1 | CTRL_B5_UNUSED)
#define CTRL_CARD_SELECT (CTRL_IDLE & ~CTRL_CARD_nCE1)
#define CTRL_CARD_RELEASE (CTRL_IDLE & ~CTRL_CS2)
#define CTRL_62256_SELECT (CTRL_IDLE & ~CTRL_CS2)
#define CTRL_62256_RELEASE CTRL_IDLE

// =====================================================================
// SPI SRAM 23LC512 (64 KB、/CS = D6)。本体だけの処理で使うのは 0x0000-0x7FFF
// =====================================================================
#define SRAM23_READ 0x03   // 読み出し: 03 アドレス上位 下位 → データ
#define SRAM23_WRITE 0x02  // 書き込み: 02 アドレス上位 下位 データ
#define SRAM23_RDMR 0x05   // モードレジスタ読み出し
#define SRAM23_WRMR 0x01   // モードレジスタ書き込み
#define SRAM23_MODE_MASK 0xC0
#define SRAM23_MODE_BYTE 0x00
#define SRAM23_MODE_PAGE 0x80
#define SRAM23_MODE_SEQUENTIAL 0x40  // 電源投入時の値。起動時にこれに揃える

#endif  // FSU_CONFIG_H
