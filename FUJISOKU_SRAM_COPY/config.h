// FUJISOKU SRAM Utility - 設定・定数・共通定義
#ifndef FSU_CONFIG_H
#define FSU_CONFIG_H

#include <Arduino.h>

#define FSU_VERSION "2.2"

// ---------------------------------------------------------------------
// オプション部品 (1 = 使う、0 = 使わない)
//   必須: MCP23S17 x 2、FUJISOKU カードのスロット、SPI SRAM (データの一時保管)
//   カードの吸い出し・書き込み・比較は Web アプリ (PC) でできるので、SD カードと LCD はなくてもよい
// ---------------------------------------------------------------------
// SD カード (本体だけでの保存・照合・書き戻し)。
//   0 にすると SdFat ライブラリも不要になり、sd コマンドと B L V S W は ERR DISABLED を返す。
//   1 のときは、起動時にカードが無くても、処理の合間に抜き差ししても動く。
#ifndef USE_SD
#define USE_SD 1
#endif

// 20 文字 x 4 行のキャラクタ LCD (I2C)。
//   0 にすると LiquidCrystal I2C ライブラリも不要になる。
//   1 のままでも、起動時に LCD が見つからなければ表示を飛ばして動く (Web アプリには同じ内容が出る)。
#ifndef USE_LCD
#define USE_LCD 1
#endif

// 動作確認用の並列 SRAM 62256 (カードと同じバスにつなぐ)。
//   1 にすると、コマンドの対象 p として読み書き・コピー・比較・バスの配線試験 (test p) に使える。
#ifndef USE_SRAM62256
#define USE_SRAM62256 0
#endif

// ---------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------
// LCD の I2C アドレス (PCF8574 バックパック)。基板によって 0x27 または 0x3F が多い。
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif
#define LCD_COLS 20
#define LCD_ROWS 4

// 結果画面を表示しておく時間 [ms] (過ぎたら待機画面に戻る)
#ifndef RESULT_HOLD_MS
#define RESULT_HOLD_MS 15000
#endif

// MCP23S17 / SPI SRAM を操作するときの SPI クロック [Hz]
#ifndef SPI_BUS_HZ
#define SPI_BUS_HZ 4000000
#endif

// 定期的な SD カード生存確認の間隔 [ms] (カードが READY のときだけ行う)
#ifndef SD_ALIVE_CHECK_MS
#define SD_ALIVE_CHECK_MS 1500
#endif

// ---------------------------------------------------------------------
// データ量
// ---------------------------------------------------------------------
#define CARD_SIZE 0x8000UL  // 扱う範囲 0x0000-0x7FFF (32 KB)
#define CHUNK_BYTES 64      // PC とのやりとりの 1 行あたりのバイト数
#define BLOCK_BYTES 256     // SD/コピー処理の 1 ブロック
#define SRAM_ERASE 0xFF

// バックアップ中に使う一時ファイル名 (成功したら本来の名前に付け替える)
#define SD_TMP_NAME "FSU_TMP.TMP"

// 転送対象 (dev 引数)。コマンドでは f / s / p
#define DEV_SPI_SRAM 0   // s: SPI SRAM (必須)
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

// SPI GPIO(MCP23S17 GPIOA, GPIOB) Definition
#define MCP23x17_OPE_WRITE 0x40
#define MCP23x17_OPE_WRITE_A2 0x48
#define MCP23x17_OPE_READ 0x41

#define MCP_ALL_INPUT 0xFF
#define MCP_ALL_OUTPUT 0x00

// register address (BANK=1)
// PORT A
#define MCP23x17_DDR_A 0x00      //  Data Direction Register A
#define MCP23x17_POL_A 0x01      //  Input Polarity A
#define MCP23x17_GPINTEN_A 0x02  //  Interrupt enable
#define MCP23x17_DEFVAL_A 0x03   //  Interrupt default
#define MCP23x17_INTCON_A 0x04   //  Interrupt control register
#define MCP23x17_IOCR 0x05       //  IO control register
#define MCP23x17_GPUP_A 0x06     //  Pull Up Resistors A
#define MCP23x17_INTF_A 0x07     //  Interrupt flag register
#define MCP23x17_INTCAP_A 0x08   //  Interrupt capture register
#define MCP23x17_GPIO_A 0x09     //  General Purpose IO A
#define MCP23x17_OLAT_A 0x0A     //  output latch
// PORT B
#define MCP23x17_DDR_B 0x10      //  Data Direction Register B
#define MCP23x17_POL_B 0x11      //  Input Polarity B
#define MCP23x17_GPINTEN_B 0x12  //  Interrupt enable
#define MCP23x17_DEFVAL_B 0x13   //  Interrupt default
#define MCP23x17_INTCON_B 0x14   //  Interrupt control register
#define MCP23x17_IOCR_B 0x15     //  IO control register
#define MCP23x17_GPUP_B 0x16     //  Pull Up Resistors B
#define MCP23x17_INTF_B 0x17     //  Interrupt flag register
#define MCP23x17_INTCAP_B 0x18   //  Interrupt capture register
#define MCP23x17_GPIO_B 0x19     //  General Purpose IO B
#define MCP23x17_OLAT_B 0x1A     //  output latch

//  Initial IOCR (BANK=0)
#define MCP23x17_IOCR_BANK0 0x0A

//  IOCR = IO CONTROL REGISTER bit masks
#define IOCR_BANK1 0x80   // BANK=1
#define IOCR_MIRROR 0x40  // INT Pins Mirror bit.
#define IOCR_SEQOP 0x20   // Sequential Operation mode bit.
#define IOCR_DISSLW 0x10  // Slew Rate control bit for SDA output.
#define IOCR_HAEN 0x08    // Hardware Address Enable bit (MCP23S17 only).
#define IOCR_ODR 0x04     // Configures the INT pin as an open-drain output.
#define IOCR_INTPOL 0x02  // This bit sets the polarity of the INT output pin.
#define IOCR_NI 0x01      // Not implemented.

// SPI SRAM Definition
#define SRAM_OPE_READ 0x03
#define SRAM_OPE_WRITE 0x02
#define SRAM_OPE_MODE 0xFF
#define REG_READ 0x05
#define REG_WRITE 0x01
#define MODE_RW_BYTE 0x00
#define MODE_RW_PAGE 0x40  // 32byte onetime
#define MODE_RW_SEQ 0x80

// SPI Chip Select pin number (LCD を使うときは I2C: SDA = A4, SCL = A5)
#define CS_SDcard 7  // USE_SD = 0 でも High にしておく (モジュールが付いたままでも SPI を邪魔しない)
#define CS_SRAM_512 6
#define CS_RESET 5
#define CS_MCP23S17 4
#define CS_FSRAM_512 1

#define IRQ_MCP 3

// MCP Ch (MCP23S17 GPIOA, GPIOB)
#define CH_MCP_BUS_ADDRESS 6
#define CH_MCP_BUS_DATA 7
#define CH_MCP_CONTROL 7

// all INPUT PULL UP
#define MCP7_PORT_PULUP 0b11111111

// ---------------------------------------------------------------------
// ch7 GPIOB (B7-B0) の制御値
// ---------------------------------------------------------------------
// B7 A17 / B6 A16 / B5 nCS_SPI_SRAM / B4 nCS1_FUJISOKU / B3 nWE / B2 nOE_62256 / B1 nOE_FUJISOKU / B0 CS2_FUJISOKU (= nCS_62256)
#define MCP_IODIR_PE1_CONNECT 0b11011011
#define MCP_IODIR_FUJISOKU_CONNECT 0b00000000
#define MCP_IODIR_SRAM_CONNECT 0b00000000
#define DISENABLE_SRAM 0b00111111
#define DISENABLE_FUJISOKU 0b00111110
#define AND_ENABLE_nWE 0b11110111
#define ENABLE_SRAM_nCS 0b00111110
#define AND_ENABLE_SRAM_nOE 0b11111011
#define ENABLE_FUJISOKU_nCS1_CS2 0b00101111
#define AND_ENABLE_FUJISOKU_nOE 0b11111101
#define AND_ENABLE_SPI_SRAM 0b11011111
#define CTRL_IDLE DISENABLE_SRAM

#endif  // FSU_CONFIG_H
