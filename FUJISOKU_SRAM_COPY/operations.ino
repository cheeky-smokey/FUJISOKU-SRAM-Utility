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
static char sd_fname[24];

// ---------------------------------------------------------------------
// 対象メモリの 1 バイト読み書き
//   DEV_FUJISOKU  (f) : FUJISOKU メモリカード
//   DEV_SPI_SRAM  (s) : SPI SRAM (データの一時保管、必須)
//   DEV_SRAM62256 (p) : 並列 SRAM 62256 (動作確認用、USE_SRAM62256 のときだけ)
// ---------------------------------------------------------------------
uint8_t dev_read(uint8_t dev, unsigned long address) {
  if (dev == DEV_FUJISOKU) return read_fujisoku(address);
#if USE_SRAM62256
  if (dev == DEV_SRAM62256) return read_sram(address);
#endif
  return read_spi_sram_byte((unsigned int)address);
}

void dev_write(uint8_t dev, unsigned long address, uint8_t dat) {
  if (dev == DEV_FUJISOKU) write_fujisoku(address, dat);
#if USE_SRAM62256
  else if (dev == DEV_SRAM62256) write_sram(address, dat);
#endif
  else write_spi_sram_byte((unsigned int)address, dat);
}

const char *dev_name(uint8_t dev) {
  if (dev == DEV_FUJISOKU) return "Card";
  if (dev == DEV_SRAM62256) return "62256";
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

// 中断の確認 (PC からの '!')。処理中に届いたその他の入力は捨てる
bool op_poll_cancel() {
  while (Serial.available() > 0) {
    if (Serial.read() == '!') op_cancel = true;
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
  ui_progress_begin(title, sub, total, false);
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
  return res;
}

// 中断で終わる
uint8_t op_cancelled(uint32_t done) {
  char m[40];
  snprintf(m, sizeof(m), "Stopped at 0x%04lX", (unsigned long)done);
  return op_finish(RES_CANCEL, "CANCEL", m);
}

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

// ---------------------------------------------------------------------
// FUJISOKU <-> SPI SRAM のコピー / 比較
// ---------------------------------------------------------------------

// src -> dst のコピー。R は Card -> SPI SRAM、C は SPI SRAM -> Card
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

// X: SPI SRAM と Card の比較
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

// E / e: メモリ全体を 0xFF で埋める
uint8_t op_erase(uint8_t dev) {
  op_start("fill", "ERASE", dev_name(dev), CARD_SIZE);
  op_phase("Erasing");
  for (uint32_t a = 0; a < CARD_SIZE; a += BLOCK_BYTES) {
    for (uint16_t i = 0; i < BLOCK_BYTES; i++) dev_write(dev, a + i, SRAM_ERASE);
    op_step(a + BLOCK_BYTES);
    if (op_cancel) return op_cancelled(a + BLOCK_BYTES);
  }
  return op_finish(RES_OK, "", "32768 bytes = FF");
}

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
  op_phase(dev == DEV_FUJISOKU ? "Reading card" : (dev == DEV_SRAM62256 ? "Reading 62256" : "Reading SPI SRAM"));
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
  op_phase(dev == DEV_FUJISOKU ? "Writing card" : (dev == DEV_SRAM62256 ? "Writing 62256" : "Writing SPI SRAM"));
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

// ---------------------------------------------------------------------
// SPI SRAM 自己診断 (T)
// ---------------------------------------------------------------------
// 0x0000-0x000F と 0x7FF0-0x7FFF を書き込み→読み戻しして /CS 経路ごとに判定。元の内容は復元する。
// path 0: /CS = Arduino D6, path 1: /CS = MCP23S17 GPB5
uint8_t op_test_spi() {
  const unsigned int base[2] = { 0x0000, 0x7FF0 };
  bool pass[2] = { false, false };
  char m[40];

  op_start("test", "SPI SRAM TEST", "D6 and MCP B5", 2);
  for (uint8_t path = 0; path < 2; path++) {
    uint8_t keep[32];
    unsigned int bad = 0;
    uint8_t first16[16];
    op_phase(path ? "Testing /CS = MCP B5" : "Testing /CS = D6");

    Serial.print("T path=");
    Serial.print(path);
    Serial.print(" rdmr=");
    Serial.println(spi_sram_rdmr(path), HEX);

    for (uint8_t k = 0; k < 2; k++)
      for (uint8_t i = 0; i < 16; i++) keep[k * 16 + i] = spi_sram_rd(path, base[k] + i);

    for (uint8_t pass_no = 0; pass_no < 2; pass_no++) {
      for (uint8_t k = 0; k < 2; k++) {
        for (uint8_t i = 0; i < 16; i++) {
          uint8_t pat = (uint8_t)(0xA0 + i);
          if (pass_no) pat = (uint8_t)~pat;
          spi_sram_wr(path, base[k] + i, pat);
        }
        for (uint8_t i = 0; i < 16; i++) {
          uint8_t pat = (uint8_t)(0xA0 + i);
          if (pass_no) pat = (uint8_t)~pat;
          uint8_t r = spi_sram_rd(path, base[k] + i);
          if (r != pat) bad++;
          if (pass_no == 0 && k == 0) first16[i] = r;
        }
      }
    }

    for (uint8_t k = 0; k < 2; k++)
      for (uint8_t i = 0; i < 16; i++) spi_sram_wr(path, base[k] + i, keep[k * 16 + i]);

    Serial.print("T path=");
    Serial.print(path);
    Serial.print(" readback=");
    for (uint8_t i = 0; i < 16; i++) {
      if (first16[i] < 0x10) Serial.print('0');
      Serial.print(first16[i], HEX);
    }
    Serial.print(" bad=");
    Serial.println(bad);
    pass[path] = (bad == 0);
    op_step(path + 1);
    if (op_cancel) return op_cancelled(path + 1);
  }
  snprintf(m, sizeof(m), "D6=%s MCP=%s", pass[0] ? "PASS" : "FAIL", pass[1] ? "PASS" : "FAIL");
  return op_finish((pass[0] || pass[1]) ? RES_OK : RES_NG, "", m);
}

#if USE_SRAM62256
// ---------------------------------------------------------------------
// test p: 62256 を使ったバスの配線試験 (カードを使わずに MCP23S17 と共通バスを確かめる)
//   データ線   : 0x0000 に 1 ビットずつ立てた値などを書いて読み戻す
//   アドレス線 : 0x0000 と 2^n (n = 0..14) の 16 か所で、ほかの場所に影響しないかを確かめる
//                (線の H / L 固定やショートがあると、別の場所の値が変わる)
// 62256 の中身は壊れる (動作確認用なので戻さない)
// ---------------------------------------------------------------------
uint8_t op_test_62256() {
  static const uint8_t pats[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x55, 0xAA, 0x00, 0xFF };
  uint8_t bad_data = 0;
  uint16_t bad_addr = 0;
  char m[48];
  op_start("test", "62256 TEST", "Bus check with 62256", 2);

  op_phase("Data lines D0-D7");
  for (uint8_t i = 0; i < sizeof(pats); i++) {
    write_sram(0, pats[i]);
    bad_data |= (uint8_t)(read_sram(0) ^ pats[i]);
  }
  op_step(1);

  op_phase("Address lines A0-A14");
  for (uint8_t n = 0; n < 15; n++) write_sram(1UL << n, 0x55);
  write_sram(0, 0xAA);
  for (uint8_t n = 0; n < 15; n++)
    if (read_sram(1UL << n) != 0x55) bad_addr |= (uint16_t)(1U << n);  // H 固定
  write_sram(0, 0x55);
  for (uint8_t n = 0; n < 15; n++) {
    write_sram(1UL << n, 0xAA);
    if (read_sram(0) != 0x55) bad_addr |= (uint16_t)(1U << n);  // L 固定
    for (uint8_t k = 0; k < 15; k++)
      if (k != n && read_sram(1UL << k) != 0x55) bad_addr |= (uint16_t)((1U << n) | (1U << k));  // ショート
    write_sram(1UL << n, 0x55);
    op_poll_cancel();
    if (op_cancel) return op_cancelled(1);
  }
  op_step(2);

  Serial.print("T 62256 data_bad=0x");
  if (bad_data < 0x10) Serial.print('0');
  Serial.print(bad_data, HEX);
  Serial.print(" addr_bad=0x");
  Serial.println((unsigned long)bad_addr, HEX);
  if (bad_data == 0 && bad_addr == 0) return op_finish(RES_OK, "", "D0-D7 OK A0-A14 OK");
  snprintf(m, sizeof(m), "data bad=0x%02X addr bad=0x%04X", bad_data, bad_addr);
  return op_finish(RES_NG, "", m);
}
#endif
