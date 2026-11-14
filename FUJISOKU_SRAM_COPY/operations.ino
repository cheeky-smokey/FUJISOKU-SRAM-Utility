// 処理 (コピー・比較・SD 保存/書き込み/照合・自己診断・消去)
//
// すべての処理は op_start() で始まり op_finish() で終わる。
//  - 進捗は "P <done> <total>"、段階の名前は "S <text>" として PC に送り、同時に LCD に表示する
//  - 終了は "OK <処理>" / "NG <処理>" / "ERR <コード> <文字>" の 1 行
//  - 処理中に PC から '!' を受け取ると中断する

static uint8_t blockbuf[BLOCK_BYTES];
static const char *op_name = "";
static uint32_t op_total = 0;
static uint32_t op_last_ms = 0;
#if USE_SD
static char sd_fname[24];
#endif

// ---------------------------------------------------------------------
// 対象メモリの 1 バイト読み書き
//   DEV_FUJISOKU  (f) : FUJISOKU メモリカード
//   DEV_SPI_SRAM  (s) : SPI SRAM (一時置き場、USE_SPI_SRAM のときだけ)
// 使えない対象は呼ぶ前に protocol.ino の dev_ready() ではじく
// ---------------------------------------------------------------------
uint8_t dev_read(uint8_t dev, uint32_t address) {
  if (dev == DEV_FUJISOKU) return read_fujisoku(address);
#if USE_SPI_SRAM
  if (dev == DEV_SPI_SRAM) return read_spi_sram_byte((uint16_t)address);
#endif
  return 0xFF;
}

void dev_write(uint8_t dev, uint32_t address, uint8_t dat) {
  if (dev == DEV_FUJISOKU) write_fujisoku(address, dat);
#if USE_SPI_SRAM
  else if (dev == DEV_SPI_SRAM) write_spi_sram_byte((uint16_t)address, dat);
#endif
}

// SPI SRAM の状態: "ok" (使える) / "none" (見つからない) / "off" (USE_SPI_SRAM = 0)
const char *spi_sram_status() {
#if USE_SPI_SRAM
  return spi_sram_present ? "ok" : "none";
#else
  return "off";
#endif
}

// PC からの rd / wr / fill で使える大きさ
uint32_t dev_size(uint8_t dev) {
  if (dev == DEV_FUJISOKU) return CARD_MAX_SIZE;
  return SPI_SRAM_SIZE;
}

const char *dev_name(uint8_t dev) {
  if (dev == DEV_FUJISOKU) return "Card";
  return "SPI SRAM";
}

// "Card -> SPI SRAM" のような LCD 2 行目の文字列 (ui_progress_begin がコピーするので一時的でよい)
static char op_sub_buf[24];
static const char *op_sub(const char *a, const char *arrow, const char *b) {
  snprintf(op_sub_buf, sizeof(op_sub_buf), "%s %s %s", a, arrow, b);
  return op_sub_buf;
}

// ---------------------------------------------------------------------
// 処理の開始・進捗・終了
// ---------------------------------------------------------------------

// 中断の確認 (PC からの '!')。処理中に届いたその他の入力は捨てる。
// カードを使う処理の途中でカードが抜かれたときも中断にする (op_card_lost が立つ)
bool op_poll_cancel() {
  while (Serial.available() > 0) {
    if (Serial.read() == '!') op_cancel = true;
  }
  if (op_needs_card && card_check_lost()) {
    op_cancel = true;
    op_card_lost = true;
  }
  return op_cancel;
}

void op_start(const char *name, const char *title, const char *sub, uint32_t total) {
  op_name = name;
  op_total = total;
  op_cancel = false;
  op_session = 1;
  op_session_ms = millis();
  op_last_ms = 0;
  op_msg[0] = '\0';
  ui_progress_begin(title, sub, total);
}

// 段階の名前を変える (LCD と PC に表示)
void op_phase(const char *text) {
  Serial.print("S ");
  Serial.println(text);
  ui_progress_phase(text);
}

// 進捗を更新する。中断が要求されたら op_cancel が立つ
void op_step(uint32_t done) {
  uint32_t now = millis();
  op_session_ms = now;
  if (now - op_last_ms >= 100 || done >= op_total) {
    op_last_ms = now;
    Serial.print("P ");
    Serial.print((unsigned long)done);
    Serial.print(' ');
    Serial.println((unsigned long)op_total);
    ui_progress_set(done);
  }
  op_poll_cancel();
}

// 終了。res: RES_OK / RES_NG / RES_ERR / RES_CANCEL。code: エラーコード、msg: 説明 (ASCII)
uint8_t op_finish(uint8_t res, const char *code, const char *msg) {
  bus_idle();  // アドレス・データ線を入力に戻し、制御線をすべて非選択にする (カードの抜き差しに備える)
  strncpy(op_msg, msg, sizeof(op_msg) - 1);
  op_msg[sizeof(op_msg) - 1] = '\0';
  switch (res) {
    case RES_OK:
    case RES_NG:
      Serial.print(res == RES_OK ? "OK " : "NG ");
      Serial.print(op_name);
      if (op_msg[0]) {
        Serial.print(' ');
        Serial.print(op_msg);
      }
      Serial.println();
      break;
    case RES_CANCEL:
      Serial.println("ERR CANCEL cancelled");
      break;
    default:
      Serial.print("ERR ");
      Serial.print(code);
      Serial.print(' ');
      Serial.println(op_msg);
      break;
  }
  ui_progress_end(res, code, op_msg);
  op_session = 0;
  op_cancel = false;
  op_needs_card = false;
  op_card_lost = false;
  return res;
}

// 中断で終わる (カードが抜かれたときは ERR NOCARD)
uint8_t op_cancelled(uint32_t done) {
  char m[40];
  if (op_card_lost) {
    snprintf(m, sizeof(m), "Card removed at 0x%04lX", (unsigned long)done);
    return op_finish(RES_ERR, "NOCARD", m);
  }
  snprintf(m, sizeof(m), "Stopped at 0x%04lX", (unsigned long)done);
  return op_finish(RES_CANCEL, "CANCEL", m);
}

#if USE_SD
// SD の失敗を「SDERR (カードは生きている)」か「SDLOST (抜かれた)」に分類して終わる
uint8_t op_sd_error(const char *what) {
  if (myFile.isOpen()) myFile.close();
  if (sd_alive()) return op_finish(RES_ERR, "SDERR", what);
  sd_lost();
  return op_finish(RES_ERR, "SDLOST", "SD card removed");
}

// 一時ファイルを閉じて消す (カードが生きていれば)
void sd_drop_tmp() {
  if (myFile.isOpen()) myFile.close();
  if (sd_alive()) SD.remove(SD_TMP_NAME);
}

// SD を使う処理の準備: マウントとファイル名の確認。失敗したら終了処理まで済ませて false
bool op_sd_prepare(const char *base) {
  op_phase("Checking SD card");
  if (!sd_make_name(base, sd_fname, sizeof(sd_fname))) {
    op_finish(RES_ERR, "ARG", "Bad file name");
    return false;
  }
  if (!sd_mount()) {
    op_finish(RES_ERR, "NOSD", "No SD card");
    return false;
  }
  return true;
}
#endif  // USE_SD

// ---------------------------------------------------------------------
// メモリ同士のコピー / 比較 / 消去
// ---------------------------------------------------------------------

// src -> dst のコピー (R = カード -> SPI SRAM、C = SPI SRAM -> カード)
uint8_t op_copy(uint8_t src, uint8_t dst) {
  const char *title = dst == DEV_FUJISOKU ? "WRITE CARD" : (src == DEV_FUJISOKU ? "READ CARD" : "COPY");
  op_start("copy", title, op_sub(dev_name(src), "->", dev_name(dst)), CARD_SIZE);
  op_phase(dst == DEV_FUJISOKU ? "Writing card" : (src == DEV_FUJISOKU ? "Reading card" : "Copying"));
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) dev_write(dst, a + i, dev_read(src, a + i));
    op_step(a + BLOCK_BYTES);
    if (op_cancel) return op_cancelled(a + BLOCK_BYTES);
  }
  return op_finish(RES_OK, "", "32768 bytes");
}

// da と db の比較 (X = SPI SRAM とカード)。最初の 8 か所の違いを "M <アドレス> <da の値> <db の値>" で送る
uint8_t op_compare(uint8_t da, uint8_t db) {
  uint32_t mismatch = 0, first = 0;
  op_start("cmp", "COMPARE", op_sub(dev_name(da), "<->", dev_name(db)), CARD_SIZE);
  op_phase("Comparing");
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) {
      uint8_t x = dev_read(da, a + i);
      uint8_t y = dev_read(db, a + i);
      if (x != y) {
        if (mismatch == 0) first = a + i;
        if (mismatch < 8) {
          Serial.print("M ");
          Serial.print((unsigned long)(a + i), HEX);
          Serial.print(' ');
          Serial.print(x, HEX);
          Serial.print(' ');
          Serial.println(y, HEX);
        }
        mismatch++;
      }
    }
    op_step(a + BLOCK_BYTES);
    if (op_cancel) return op_cancelled(a + BLOCK_BYTES);
  }
  char m[40];
  if (mismatch == 0) return op_finish(RES_OK, "", "all 32768 bytes match");
  snprintf(m, sizeof(m), "mismatches=%lu first=0x%04lX", (unsigned long)mismatch, (unsigned long)first);
  return op_finish(RES_NG, "", m);
}

// メモリ全体を ERASE_VALUE (0xFF) で埋める (E = SPI SRAM、e = カード)
uint8_t op_erase(uint8_t dev) {
  op_start("fill", "ERASE", dev_name(dev), CARD_SIZE);
  op_phase("Erasing");
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) dev_write(dev, a + i, ERASE_VALUE);
    op_step(a + BLOCK_BYTES);
    if (op_cancel) return op_cancelled(a + BLOCK_BYTES);
  }
  return op_finish(RES_OK, "", "32768 bytes = FF");
}

#if USE_SD
// ---------------------------------------------------------------------
// SD カードとの間の保存 / 書き込み / 照合
// ---------------------------------------------------------------------

// メモリ -> SD の保存 (S / B)
uint8_t op_sd_save(uint8_t dev, const char *base) {
  char m[40];
  op_start("sd", dev == DEV_FUJISOKU ? "BACKUP" : "SAVE", op_sub(dev_name(dev), "->", "SD"), CARD_SIZE);
  if (!op_sd_prepare(base)) return RES_ERR;
  // いったん一時ファイルに書き、最後に名前を付け替える。
  // 失敗・中断・SD の抜けがあっても、これまでのバックアップは壊れない。
  op_phase("Opening file");
  if (!myFile.open(SD_TMP_NAME, O_WRONLY | O_CREAT | O_TRUNC)) return op_sd_error("Cannot create file");
  op_phase(dev == DEV_FUJISOKU ? "Reading card" : "Reading SPI SRAM");
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) blockbuf[i] = dev_read(dev, a + i);
    if (myFile.write(blockbuf, BLOCK_BYTES) != (int)BLOCK_BYTES) {
      sd_drop_tmp();
      return op_sd_error("SD write error");
    }
    op_step(a + BLOCK_BYTES);
    if (op_cancel) {
      sd_drop_tmp();
      return op_cancelled(a + BLOCK_BYTES);
    }
  }
  op_phase("Closing file");
  if (!myFile.close()) {
    sd_drop_tmp();
    return op_sd_error("SD close error");
  }
  op_phase("Saving file");
  if (SD.exists(sd_fname) && !SD.remove(sd_fname)) {
    sd_drop_tmp();
    return op_sd_error("Cannot replace file");
  }
  if (!SD.rename(SD_TMP_NAME, sd_fname)) return op_sd_error("Cannot rename file");
  snprintf(m, sizeof(m), "32768 bytes -> %s", sd_fname);
  return op_finish(RES_OK, "", m);
}

// 開いているファイルの先頭から CARD_SIZE バイトをメモリと比べる。
// progress_base: 進捗の起点。戻り値: 不一致数。*io_err が立てば SD の読み出し失敗
uint32_t sd_compare_pass(uint8_t dev, uint32_t progress_base, uint32_t *first, bool *io_err) {
  uint32_t mismatch = 0;
  *first = 0;
  *io_err = false;
  myFile.seekSet(0);
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    if (myFile.read(blockbuf, BLOCK_BYTES) != (int)BLOCK_BYTES) {
      *io_err = true;
      return mismatch;
    }
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) {
      uint8_t mem = dev_read(dev, a + i);
      if (mem != blockbuf[i]) {
        if (mismatch == 0) *first = a + i;
        if (mismatch < 8) {
          Serial.print("M ");
          Serial.print((unsigned long)(a + i), HEX);
          Serial.print(' ');
          Serial.print(blockbuf[i], HEX);
          Serial.print(' ');
          Serial.println(mem, HEX);
        }
        mismatch++;
      }
    }
    op_step(progress_base + a + BLOCK_BYTES);
    if (op_cancel) return mismatch;
  }
  return mismatch;
}

// 結果を NG/OK にまとめる
uint8_t sd_compare_result(uint32_t mismatch, uint32_t first) {
  char m[40];
  if (mismatch == 0) return op_finish(RES_OK, "", "all 32768 bytes match");
  snprintf(m, sizeof(m), "mismatches=%lu first=0x%04lX", (unsigned long)mismatch, (unsigned long)first);
  return op_finish(RES_NG, "", m);
}

// SD -> メモリの書き込み (W / L)。verify_after: 書き込み後にもう一度読んで照合する
uint8_t op_sd_load(uint8_t dev, const char *base, bool verify_after) {
  char m[40];
  uint32_t mismatch = 0, first = 0;
  bool io_err = false;
  op_start("sd", dev == DEV_FUJISOKU ? "RESTORE" : "LOAD", op_sub("SD", "->", dev_name(dev)), verify_after ? CARD_SIZE * 2 : CARD_SIZE);
  if (!op_sd_prepare(base)) return RES_ERR;
  op_phase("Opening file");
  if (!SD.exists(sd_fname)) return op_finish(RES_ERR, "NOFILE", "File not found");
  if (!myFile.open(sd_fname, O_RDONLY)) return op_sd_error("Cannot open file");
  uint32_t size = (uint32_t)myFile.fileSize();
  if (size != CARD_SIZE) {
    myFile.close();
    snprintf(m, sizeof(m), "File is %lu bytes", (unsigned long)size);
    return op_finish(RES_ERR, "BADSIZE", m);
  }
  op_phase(dev == DEV_FUJISOKU ? "Writing card" : "Writing SPI SRAM");
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    if (myFile.read(blockbuf, BLOCK_BYTES) != (int)BLOCK_BYTES) return op_sd_error("SD read error");
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) dev_write(dev, a + i, blockbuf[i]);
    op_step(a + BLOCK_BYTES);
    if (op_cancel) {
      myFile.close();
      return op_cancelled(a + BLOCK_BYTES);
    }
  }
  if (verify_after) {
    op_phase("Verifying");
    mismatch = sd_compare_pass(dev, CARD_SIZE, &first, &io_err);
    myFile.close();
    if (io_err) return op_sd_error("SD read error");
    if (op_cancel) return op_cancelled(CARD_SIZE);
    return sd_compare_result(mismatch, first);
  }
  myFile.close();
  snprintf(m, sizeof(m), "32768 bytes <- %s", sd_fname);
  return op_finish(RES_OK, "", m);
}

// SD とメモリの照合 (V)
uint8_t op_sd_verify(uint8_t dev, const char *base) {
  char m[40];
  uint32_t mismatch = 0, first = 0;
  bool io_err = false;
  op_start("sd", "VERIFY", op_sub("SD", "<->", dev_name(dev)), CARD_SIZE);
  if (!op_sd_prepare(base)) return RES_ERR;
  op_phase("Opening file");
  if (!SD.exists(sd_fname)) return op_finish(RES_ERR, "NOFILE", "File not found");
  if (!myFile.open(sd_fname, O_RDONLY)) return op_sd_error("Cannot open file");
  uint32_t size = (uint32_t)myFile.fileSize();
  if (size != CARD_SIZE) {
    myFile.close();
    snprintf(m, sizeof(m), "File is %lu bytes (need 32768)", (unsigned long)size);
    return op_finish(RES_NG, "BADSIZE", m);
  }
  op_phase("Comparing");
  mismatch = sd_compare_pass(dev, 0, &first, &io_err);
  myFile.close();
  if (io_err) return op_sd_error("SD read error");
  if (op_cancel) return op_cancelled(CARD_SIZE);
  return sd_compare_result(mismatch, first);
}

// SD のファイル一覧 (*.bin)
uint8_t op_sd_list() {
  char m[40];
  op_start("sd", "SD FILES", "List *.bin", 1);
  op_phase("Checking SD card");
  if (!sd_mount()) return op_finish(RES_ERR, "NOSD", "No SD card");
  int n = sd_list_bin();
  if (n < 0) return op_sd_error("Cannot read directory");
  snprintf(m, sizeof(m), "%d files", n);
  return op_finish(RES_OK, "", m);
}

// SD カードの再検出
uint8_t op_sd_check() {
  op_start("sdcheck", "SD CHECK", "Detect card", 1);
  op_phase("Checking SD card");
  if (!sd_mount()) return op_finish(RES_ERR, "NOSD", "No SD card");
  return op_finish(RES_OK, "", "SD card ready");
}
#endif  // USE_SD

#if USE_SPI_SRAM
// ---------------------------------------------------------------------
// SPI SRAM 自己診断 (test / T)。USE_SPI_SRAM のときだけ
//   1. SPI SRAM があるかを確かめ直し (起動後につないだ場合も認識する)、モードレジスタを読む
//      (シーケンシャル 0x40 になっていれば応答あり。0xFF なら応答なし)
//   2. 両端の 16 バイト (0x0000-0x000F、0x7FF0-0x7FFF) に 2 種類の模様を書いて読み戻す。元の内容は戻す
//      SPI SRAM のアドレスはシリアルで送るので、配線の確認は端の読み書きで足りる
// ---------------------------------------------------------------------
#define SPI_TEST_BYTES 16

static uint8_t spi_test_pattern(uint8_t pass_no, uint8_t i) {
  uint8_t pat = (uint8_t)(0xA0 + i);
  return pass_no ? (uint8_t)~pat : pat;
}

uint8_t op_test_spi() {
  static const uint16_t base[2] = { 0x0000, 0x7FF0 };
  uint8_t keep[2][SPI_TEST_BYTES];
  uint8_t first_read[SPI_TEST_BYTES];
  unsigned int bad = 0;
  char m[48];

  op_start("test", "SPI SRAM TEST", "SPI SRAM (/CS D6)", 2);
  op_phase("Mode register");
  spi_sram_init();
  uint8_t mode = spi_sram_read_mode();
  Serial.print("T mode=");
  if (mode < 0x10) Serial.print('0');
  Serial.println(mode, HEX);
  op_step(1);

  op_phase("Read/write test");
  for (uint8_t k = 0; k < 2; k++)
    for (uint8_t i = 0; i < SPI_TEST_BYTES; i++) keep[k][i] = read_spi_sram_byte(base[k] + i);
  for (uint8_t pass_no = 0; pass_no < 2; pass_no++) {
    for (uint8_t k = 0; k < 2; k++) {
      for (uint8_t i = 0; i < SPI_TEST_BYTES; i++) write_spi_sram_byte(base[k] + i, spi_test_pattern(pass_no, i));
      for (uint8_t i = 0; i < SPI_TEST_BYTES; i++) {
        uint8_t r = read_spi_sram_byte(base[k] + i);
        if (r != spi_test_pattern(pass_no, i)) bad++;
        if (pass_no == 0 && k == 0) first_read[i] = r;
      }
    }
  }
  for (uint8_t k = 0; k < 2; k++)
    for (uint8_t i = 0; i < SPI_TEST_BYTES; i++) write_spi_sram_byte(base[k] + i, keep[k][i]);

  // 0x0000 から読み戻した 16 バイト (正しければ A0A1...AF) と、違ったバイト数
  Serial.print("T readback=");
  for (uint8_t i = 0; i < SPI_TEST_BYTES; i++) {
    if (first_read[i] < 0x10) Serial.print('0');
    Serial.print(first_read[i], HEX);
  }
  Serial.print(" bad=");
  Serial.println(bad);
  op_step(2);

  if (bad == 0) return op_finish(RES_OK, "", "SPI SRAM OK");
  if (mode == 0xFF) snprintf(m, sizeof(m), "no response (check /CS D6) bad=%u", bad);
  else snprintf(m, sizeof(m), "read/write failed bad=%u", bad);
  return op_finish(RES_NG, "", m);
}
#endif  // USE_SPI_SRAM
