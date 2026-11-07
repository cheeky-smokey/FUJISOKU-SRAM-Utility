// ハードウェアアクセス: MCP23S17、FUJISOKU カード、62256 (オプション)、SPI SRAM
//
// SPI につながる部品 (どれも D13 SCK / D12 MISO / D11 MOSI を共有)
//   MCP23S17 x 2 : /CS = D4 (共通)。ハードウェアアドレス 6 と 7 で区別する
//   SPI SRAM     : /CS = D6
//   SD カード    : /CS = D7 (storage.ino)
// どのアクセスも SPI.beginTransaction() で SPI_BUS_HZ にしてから行う
// (SdFat は SPI を高速にしたままにするので、設定なしの SPI.transfer() は SD のクロックで動いてしまう)。
//
// カードと 62256 は MCP23S17 を通した並列バスにつながる
//   ch6 GPA / GPB = A0-A7 / A8-A15、ch7 GPA = D0-D7、ch7 GPB = 制御線 (config.h の CTRL_*)
// 1 バイトの手順
//   読み出し: アドレス → 選択 → /OE = L → データを読む → /OE = H → 解放
//   書き込み: データ → アドレス → 選択 → /WE = L → /WE = H (ここで書き込まれる) → 解放
// 処理が終わったら bus_idle() でアドレス・データ線を入力に戻す (カードの抜き差しに備える)。

static const SPISettings fsu_spi_settings(SPI_BUS_HZ, MSBFIRST, SPI_MODE0);

// v1 で実機確認した制御値から変わっていないことを確かめる
static_assert(CTRL_IDLE == 0x3F, "ch7 GPB idle value");
static_assert(CTRL_CARD_SELECT == 0x2F, "card select value");
static_assert(CTRL_CARD_RELEASE == 0x3E, "card release value");
static_assert(CTRL_62256_SELECT == 0x3E, "62256 select value");
static_assert(MCP_IOCON_VALUE == 0x8C, "MCP23S17 IOCON value");

// ---------------------------------------------------------------------
// MCP23S17 のレジスタ
// ---------------------------------------------------------------------
uint8_t mcp_read(uint8_t ch, uint8_t reg) {
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(PIN_CS_MCP, LOW);
  SPI.transfer(MCP_OP_READ(ch));
  SPI.transfer(reg);
  uint8_t dat = SPI.transfer(0x00);
  digitalWrite(PIN_CS_MCP, HIGH);
  SPI.endTransaction();
  return dat;
}

void mcp_write(uint8_t ch, uint8_t reg, uint8_t dat) {
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(PIN_CS_MCP, LOW);
  SPI.transfer(MCP_OP_WRITE(ch));
  SPI.transfer(reg);
  SPI.transfer(dat);
  digitalWrite(PIN_CS_MCP, HIGH);
  SPI.endTransaction();
}

// ch7 GPB (制御線) に書く
static void ctrl_write(uint8_t value) {
  mcp_write(MCP_CH_CTRL, MCP_GPIO_B, value);
}

// ---------------------------------------------------------------------
// 並列バスの向き
//   同じ向きが続くあいだは MCP の IODIR を書き直さない (1 バイトあたりの SPI 転送を減らす)。
//   bus_idle() でアドレス・データとも入力に戻る。
// ---------------------------------------------------------------------
#define BUS_IDLE 0   // アドレス・データとも入力 (待機中)
#define BUS_READ 1   // アドレス出力、データ入力
#define BUS_WRITE 2  // アドレス・データとも出力

static uint8_t bus_dir = BUS_IDLE;

static void bus_set_dir(uint8_t dir) {
  if (dir == bus_dir) return;
  if (bus_dir == BUS_IDLE) {
    mcp_write(MCP_CH_ADDR, MCP_IODIR_A, MCP_ALL_OUTPUT);  // A0-A7
    mcp_write(MCP_CH_ADDR, MCP_IODIR_B, MCP_ALL_OUTPUT);  // A8-A15
  }
  mcp_write(MCP_CH_DATA, MCP_IODIR_A, dir == BUS_WRITE ? MCP_ALL_OUTPUT : MCP_ALL_INPUT);  // D0-D7
  bus_dir = dir;
}

// バスを待機状態にする: アドレス・データ線は入力 (プルアップ)、制御線はすべて非選択
void bus_idle() {
  mcp_write(MCP_CH_DATA, MCP_IODIR_A, MCP_ALL_INPUT);
  mcp_write(MCP_CH_ADDR, MCP_IODIR_A, MCP_ALL_INPUT);
  mcp_write(MCP_CH_ADDR, MCP_IODIR_B, MCP_ALL_INPUT);
  // 先に出力ラッチへ非選択の値を書いてから出力にする (制御線が一瞬 Low になるのを避ける)
  ctrl_write(CTRL_IDLE);
  mcp_write(MCP_CH_CTRL, MCP_IODIR_B, MCP_ALL_OUTPUT);
  bus_dir = BUS_IDLE;
}

// MCP23S17 の初期化 (リセット直後に 1 回): BANK = 1、HAEN = 1、全ピン入力 + プルアップ、バスを待機状態に
void mcp_init() {
  // リセット直後は HAEN = 0 で、本来はアドレスピンに関係なく全チップが応答する。ところが A2 ピンが High の
  // チップは、オペコードの A2 ビットも 1 でないと応答しない (MCP23S17 の癖)。ch6 / ch7 は A2 = 1 なので、
  // A2 = 0 宛て (0x40) と A2 = 1 宛て (0x48) の両方で IOCON を書く。
  const uint8_t ops[2] = { MCP_OP_WRITE(0), MCP_OP_WRITE(4) };
  for (uint8_t i = 0; i < 2; i++) {
    SPI.beginTransaction(fsu_spi_settings);
    digitalWrite(PIN_CS_MCP, LOW);
    SPI.transfer(ops[i]);
    SPI.transfer(MCP_IOCON_BANK0);
    SPI.transfer(MCP_IOCON_VALUE);
    digitalWrite(PIN_CS_MCP, HIGH);
    SPI.endTransaction();
  }

  // ここからは BANK = 1 の配置。全チャネルを入力 + プルアップにする (使うのは ch6 と ch7)
  for (uint8_t ch = 0; ch < 8; ch++) {
    mcp_write(ch, MCP_OLAT_A, 0x00);
    mcp_write(ch, MCP_OLAT_B, 0x00);
    mcp_write(ch, MCP_IODIR_A, MCP_ALL_INPUT);
    mcp_write(ch, MCP_IODIR_B, MCP_ALL_INPUT);
    mcp_write(ch, MCP_GPPU_A, 0xFF);
    mcp_write(ch, MCP_GPPU_B, 0xFF);
  }
  bus_idle();
}

// ---------------------------------------------------------------------
// 並列バス (カード / 62256) の 1 バイト読み書き
// ---------------------------------------------------------------------

// アドレスを出してチップを選ぶ。sel: 選択の制御値。戻り値: 選択中の制御値 (A16 / A17 を含む)
static uint8_t bus_select(uint32_t address, uint8_t sel) {
  mcp_write(MCP_CH_ADDR, MCP_GPIO_A, (uint8_t)address);         // A0-A7
  mcp_write(MCP_CH_ADDR, MCP_GPIO_B, (uint8_t)(address >> 8));  // A8-A15
  uint8_t ctrl = sel | (uint8_t)((address >> 10) & (CTRL_A16 | CTRL_A17));  // A16 → B6、A17 → B7
  ctrl_write(ctrl);
  return ctrl;
}

// sel: 選択の制御値、oe_bit: そのチップの /OE のビット、release: 読んだあとの制御値
static uint8_t bus_read_byte(uint32_t address, uint8_t sel, uint8_t oe_bit, uint8_t release) {
  bus_set_dir(BUS_READ);
  uint8_t ctrl = bus_select(address, sel);
  ctrl_write(ctrl & ~oe_bit);                        // /OE = L: チップがデータを出す
  uint8_t dat = mcp_read(MCP_CH_DATA, MCP_GPIO_A);  // D0-D7
  ctrl_write(ctrl);                                  // /OE = H
  ctrl_write(release);
  return dat;
}

static void bus_write_byte(uint32_t address, uint8_t dat, uint8_t sel, uint8_t release) {
  mcp_write(MCP_CH_DATA, MCP_GPIO_A, dat);  // 先に出力ラッチへ (出力にしたときから正しい値が出る)
  bus_set_dir(BUS_WRITE);
  uint8_t ctrl = bus_select(address, sel);
  ctrl_write(ctrl & ~CTRL_nWE);  // /WE = L
  ctrl_write(ctrl);              // /WE = H: この立ち上がりでデータが書き込まれる
  ctrl_write(release);
}

// FUJISOKU カード (A0-A17 で最大 256 KB。このスケッチが扱うのは 0x0000-0x7FFF)
uint8_t read_fujisoku(uint32_t address) {
  return bus_read_byte(address, CTRL_CARD_SELECT, CTRL_CARD_nOE, CTRL_CARD_RELEASE);
}

void write_fujisoku(uint32_t address, uint8_t dat) {
  bus_write_byte(address, dat, CTRL_CARD_SELECT, CTRL_CARD_RELEASE);
}

#if USE_SRAM62256
// 62256 (動作確認用の並列 SRAM 32 KB、オプション)
uint8_t read_sram62256(uint32_t address) {
  return bus_read_byte(address, CTRL_62256_SELECT, CTRL_62256_nOE, CTRL_62256_RELEASE);
}

void write_sram62256(uint32_t address, uint8_t dat) {
  bus_write_byte(address, dat, CTRL_62256_SELECT, CTRL_62256_RELEASE);
}
#endif

// ---------------------------------------------------------------------
// SPI SRAM 23LC512 (/CS = D6)
// ---------------------------------------------------------------------
static void spi_sram_begin(uint8_t cmd) {
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(PIN_CS_SPI_SRAM, LOW);
  SPI.transfer(cmd);
}

static void spi_sram_end() {
  digitalWrite(PIN_CS_SPI_SRAM, HIGH);
  SPI.endTransaction();
}

uint8_t read_spi_sram_byte(uint16_t address) {
  spi_sram_begin(SRAM23_READ);
  SPI.transfer((uint8_t)(address >> 8));
  SPI.transfer((uint8_t)address);
  uint8_t dat = SPI.transfer(0x00);  // ダミーを送るあいだにデータが返る
  spi_sram_end();
  return dat;
}

void write_spi_sram_byte(uint16_t address, uint8_t dat) {
  spi_sram_begin(SRAM23_WRITE);
  SPI.transfer((uint8_t)(address >> 8));
  SPI.transfer((uint8_t)address);
  SPI.transfer(dat);
  spi_sram_end();
}

// モードレジスタを読む。起動時にシーケンシャルにするので、応答があれば 0x40。
// 0xFF なら MISO が High のまま (チップが無い、/CS の配線が違う など)
uint8_t spi_sram_read_mode() {
  spi_sram_begin(SRAM23_RDMR);
  uint8_t mode = SPI.transfer(0x00);
  spi_sram_end();
  return mode;
}

// モードを電源投入時と同じシーケンシャルに揃える (リセットだけでは前の設定が残るため)。
// 読み書きは 1 バイトずつ /CS を上げ下げするので、どのモードでも同じように動く。
void spi_sram_init() {
  spi_sram_begin(SRAM23_WRMR);
  SPI.transfer(SRAM23_MODE_SEQUENTIAL);
  spi_sram_end();
}
