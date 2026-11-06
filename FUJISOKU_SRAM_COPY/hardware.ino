// ハードウェアアクセス: MCP23S17 (GPIO エキスパンダ), FUJISOKU カード, SPI SRAM, 62256 (オプション)
//
// すべての SPI アクセスは SPI.beginTransaction() で SPI_BUS_HZ に設定してから行う。
// (SD ライブラリが SPI 設定を高速に変えたままにするため、設定なしの SPI.transfer() は
//  直前に使ったデバイスのクロックで動いてしまう)

static const SPISettings fsu_spi_settings(SPI_BUS_HZ, MSBFIRST, SPI_MODE0);

//READ GPIO register
uint8_t read_mcp_register(uint8_t ch, uint8_t address) {
  uint8_t dat;
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_READ + ch * 2);
  SPI.transfer(address);
  dat = SPI.transfer(0);
  digitalWrite(CS_MCP23S17, HIGH);
  SPI.endTransaction();
  return (dat);
}

//GPIO register write
void write_mcp_register(uint8_t ch, uint8_t address, uint8_t dat) {
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE + ch * 2);
  SPI.transfer(address);
  SPI.transfer(dat);
  digitalWrite(CS_MCP23S17, HIGH);
  SPI.endTransaction();
}

// MCP23S17 の初期化 (BANK=1, 全チャネル入力+プルアップ, バス待機状態)
void mcp_init() {
  uint8_t ch;
  //MCP23S17 BANK=1
  //アドレスA2:Hのときはレシスタ書き込みオペコード(0x40)のA2ビットを1にしないとIOCRが指定されない!!!
  //アドレス0-3(A2:0)のレシスタ書き込みオペコードは(0x40)だが
  //アドレス4-7(A2:1)のレシスタ書き込みオペコードは(0x48)となる
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE);
  SPI.transfer(MCP23x17_IOCR_BANK0);
  SPI.transfer(IOCR_BANK1 + IOCR_HAEN + IOCR_ODR);
  digitalWrite(CS_MCP23S17, HIGH);
  digitalWrite(CS_MCP23S17, LOW);
  SPI.transfer(MCP23x17_OPE_WRITE_A2);
  SPI.transfer(MCP23x17_IOCR_BANK0);
  SPI.transfer(IOCR_BANK1 + IOCR_HAEN + IOCR_ODR);
  digitalWrite(CS_MCP23S17, HIGH);
  SPI.endTransaction();

  // ONCE after RESET
  for (ch = 0; ch < 8; ch++) {
    write_mcp_register(ch, MCP23x17_OLAT_A, 0x00);
    write_mcp_register(ch, MCP23x17_OLAT_B, 0x00);
    write_mcp_register(ch, MCP23x17_DDR_A, MCP_ALL_INPUT);
    write_mcp_register(ch, MCP23x17_DDR_B, MCP_ALL_INPUT);
    write_mcp_register(ch, MCP23x17_GPUP_A, 0b11111111);  //All pins PullUp
    write_mcp_register(ch, MCP23x17_GPUP_B, 0b11111111);
  }
  bus_idle();
}

// バスを待機状態に戻す (データ/アドレス入力、制御線 全非選択)
void bus_idle() {
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_A, MCP_ALL_INPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_B, MCP_ALL_INPUT);
  // 先に GPIO_B へ非選択値を書いてから出力にする (全制御線が一瞬 Low になるのを避ける)
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, CTRL_IDLE);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_DDR_B, MCP_ALL_OUTPUT);
}

//SET SRAM Address set to MCP
void set_mcp_sram_address(unsigned long int address, uint8_t CS) {
  uint8_t addr1, addr2, addr3;

  addr1 = address & 0xff;
  addr2 = (address >> 8) & 0xff;
  addr3 = (address >> (8 + 2)) & 0b11000000;  //A17,A16
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_A, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_DDR_B, MCP_ALL_OUTPUT);
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_GPIO_A, addr1);  //A0-A7
  write_mcp_register(CH_MCP_BUS_ADDRESS, MCP23x17_GPIO_B, addr2);  //A8-A15
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, CS + addr3);  // A17 A16 + chip select
}

#if USE_SRAM62256
// 62256 (動作確認用の並列 SRAM 32KB、オプション) を 1 バイト読む
uint8_t read_sram(unsigned long int address) {
  uint8_t dat;
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);  // Data bus direction change to input
  set_mcp_sram_address(address, ENABLE_SRAM_nCS);                      // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SRAM_nOE));  // nOE Output enable
  dat = read_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A);                                // Read data
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                          // nOE Output disenable ; A17,A16
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_SRAM);                      // All control pin disable
  return (dat);
}
#endif

// Read FUJISOKU SRAM Memory card (MAX 512kB)
uint8_t read_fujisoku(unsigned long int address) {
  uint8_t dat;
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);  // Data bus direction change to input
  set_mcp_sram_address(address, ENABLE_FUJISOKU_nCS1_CS2);             // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_FUJISOKU_nOE));  // nOE Output enable
  dat = read_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A);                                    // Read dat
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                              // nOE Output disenable
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_FUJISOKU);                      // All control pin disable
  return (dat);
}

#if USE_SRAM62256
// 62256 に 1 バイト書く
void write_sram(unsigned long int address, uint8_t dat) {
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_OUTPUT);  //DATA BUS OUTPUT
  set_mcp_sram_address(address, ENABLE_SRAM_nCS);                       // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_nWE));  // write enable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A, dat);                           // Data set
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                     // write disenable
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_SRAM);                 // All control pin disable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);                  // Data bus direction change to input(Pull Up)
}
#endif

// Write FUJISOKU SRAM Memory card (MAX 512kB)
void write_fujisoku(unsigned long int address, uint8_t dat) {
  uint8_t address_cs;
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_OUTPUT);  //DATA BUS OUTPUT
  set_mcp_sram_address(address, ENABLE_FUJISOKU_nCS1_CS2);              // Address set and chip select
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_nWE));  // write enable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_GPIO_A, dat);                           // Data set
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);                     // write disenable
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, DISENABLE_FUJISOKU);             // All control pin disable
  write_mcp_register(CH_MCP_BUS_DATA, MCP23x17_DDR_A, MCP_ALL_INPUT);
}

//READ SPI SRAM register
uint8_t read_spi_sram_byte(unsigned int address) {
  uint8_t dat;
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  dat = SPI.transfer(MODE_RW_BYTE);
  digitalWrite(CS_SRAM_512, HIGH);
  SPI.endTransaction();
  return (dat);
}

//WRITE SPI SRAM register
void write_spi_sram_byte(unsigned int address, uint8_t dat) {
  SPI.beginTransaction(fsu_spi_settings);
  digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  SPI.transfer(dat);
  digitalWrite(CS_SRAM_512, HIGH);
  SPI.endTransaction();
}

//READ SPI SRAM on MCP register (/CS = MCP ch7 GPB5)
uint8_t read_spi_sram4MCP_byte(unsigned int address) {
  uint8_t dat;
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM));
  SPI.beginTransaction(fsu_spi_settings);
  SPI.transfer(SRAM_OPE_READ);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  dat = SPI.transfer(MODE_RW_BYTE);
  SPI.endTransaction();
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);
  return (dat);
}

//WRITE SPI SRAM on MCP register (/CS = MCP ch7 GPB5)
void write_spi_sram4MCP_byte(unsigned int address, uint8_t dat) {
  uint8_t address_cs;
  address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM));
  SPI.beginTransaction(fsu_spi_settings);
  SPI.transfer(SRAM_OPE_WRITE);
  SPI.transfer(address / 0x100);
  SPI.transfer(address % 0x100);
  SPI.transfer(dat);
  SPI.endTransaction();
  write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);
}

// SPI SRAM 自己診断用: path 0 = /CS が Arduino D6, path 1 = /CS が MCP23S17 GPB5
uint8_t spi_sram_rd(uint8_t path, unsigned int a) {
  return path ? read_spi_sram4MCP_byte(a) : read_spi_sram_byte(a);
}

void spi_sram_wr(uint8_t path, unsigned int a, uint8_t d) {
  if (path) write_spi_sram4MCP_byte(a, d);
  else write_spi_sram_byte(a, d);
}

// モードレジスタ読み出し (RDMR 0x05)。起動時に spi_sram_init() でバイトモードにするので、応答があれば 0x00。
// 0xFF = MISO が H のまま (チップ無応答 / /CS 未接続)
uint8_t spi_sram_rdmr(uint8_t path) {
  uint8_t dat;
  uint8_t address_cs = 0;
  if (path) {
    address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
    write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM));
  }
  SPI.beginTransaction(fsu_spi_settings);
  if (!path) digitalWrite(CS_SRAM_512, LOW);
  SPI.transfer(REG_READ);
  dat = SPI.transfer(0);
  if (!path) digitalWrite(CS_SRAM_512, HIGH);
  SPI.endTransaction();
  if (path) write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);
  return dat;
}

// SPI SRAM をバイトモードにする (WRMR 0x01, 0x00)。電源投入直後はシーケンシャルモード。
// /CS を MCP23S17 の B5 で作る配線では、B5 を戻す MCP への書き込み (3 バイト) の間もまだ /CS が
// Low のため、シーケンシャルのままだとその 3 バイトが次のアドレスに書き込まれてしまう。
// バイトモードなら 1 バイトで終わるので影響しない。どちらの /CS も試す (つながっていない側は無害)。
void spi_sram_init() {
  for (uint8_t path = 0; path < 2; path++) {
    uint8_t address_cs = 0;
    if (path) {
      address_cs = read_mcp_register(CH_MCP_CONTROL, MCP23x17_OLAT_B);
      write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, (address_cs & AND_ENABLE_SPI_SRAM));
    }
    SPI.beginTransaction(fsu_spi_settings);
    if (!path) digitalWrite(CS_SRAM_512, LOW);
    SPI.transfer(REG_WRITE);
    SPI.transfer(MODE_RW_BYTE);
    if (!path) digitalWrite(CS_SRAM_512, HIGH);
    SPI.endTransaction();
    if (path) write_mcp_register(CH_MCP_CONTROL, MCP23x17_GPIO_B, address_cs);
  }
}
