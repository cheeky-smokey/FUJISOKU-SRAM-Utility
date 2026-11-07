// SD カードの扱い (USE_SD = 1 のときだけ。0 ならこのファイルの中身はコンパイルされない)
//
// 起動時にカードが無くても停止しない。処理の合間に抜き差ししてもエラーにならない。
//  - SD を使う処理の先頭で毎回 SD.begin() をやり直す (遅延マウント)
//  - 処理中に失敗したら、カードが生きているか確認して「SDERR / SDLOST」に分類する
//  - 待機中は 1.5 秒ごとにカードの生存確認だけ行い、抜かれたら状態を ABSENT にする
//  - カードが無い状態での SD.begin() は約 2 秒かかる (SdFat の仕様) ため、
//    ABSENT のときは自動では再検出しない。SD を使う処理 / sdcheck / LCD の SD 表示タッチで再検出する

#if USE_SD

// SD.end() は UNO R4 では共有の SPI 自体を止める (SPI.end())。MCP23S17 / LCD / タッチも
// 同じ SPI を使うので、止まったままにならないよう直後に動かし直す
// (beginTransaction は未初期化なら begin() を呼ぶ)
static void spi_revive() {
  SPI.beginTransaction(fsu_spi_settings);
  SPI.endTransaction();
}

// 状態を更新し、変化があれば PC と LCD に知らせる
void sd_set_state(uint8_t s) {
  if (s == sd_state) return;
  sd_state = s;
  sd_changed = true;
  Serial.print("E SD ");
  Serial.println(s == SD_STATE_READY ? 1 : 0);
}

// 新しくマウントし直す。成功したら true
bool sd_mount() {
  if (myFile.isOpen()) myFile.close();
  if (sd_state == SD_STATE_READY) {
    SD.end();
    spi_revive();
  }
  bool ok = SD.begin(CS_SDcard);
  sd_checked_ms = millis();
  sd_set_state(ok ? SD_STATE_READY : SD_STATE_ABSENT);
  return ok;
}

// カードが抜かれた/応答しない: ファイルを閉じて ABSENT にする
void sd_lost() {
  if (myFile.isOpen()) myFile.close();
  SD.end();
  spi_revive();
  sd_set_state(SD_STATE_ABSENT);
}

// カードが応答するか (CSD レジスタを読む)
bool sd_alive() {
  if (sd_state != SD_STATE_READY) return false;
  if (!SD.card()) return false;
  return SD.card()->sectorCount() != 0;
}

// 待機中の生存確認 (READY のときだけ)
void sd_idle_check() {
  if (sd_state != SD_STATE_READY) return;
  if (millis() - sd_checked_ms < SD_ALIVE_CHECK_MS) return;
  sd_checked_ms = millis();
  if (!sd_alive()) sd_lost();
}

// ファイル名の組み立て: base (英数字 _ - のみ、16 文字まで) + ".bin"。base が空なら "fujisoku"
bool sd_make_name(const char *base, char *out, size_t n) {
  if (base == NULL || base[0] == '\0') base = "fujisoku";
  size_t len = strlen(base);
  if (len == 0 || len > 16 || n < len + 5) return false;
  for (size_t i = 0; i < len; i++) {
    char c = base[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
    if (!ok) return false;
  }
  memcpy(out, base, len);
  memcpy(out + len, ".bin", 5);
  return true;
}

// ルートの *.bin を "F <name> <size>" で出力する。件数を返す (開けなければ -1)
int sd_list_bin() {
  FsFile dir;
  FsFile f;
  char name[48];
  int n = 0;
  if (!dir.open("/")) return -1;
  while (n < 32 && f.openNext(&dir, O_RDONLY)) {
    if (!f.isDir()) {
      f.getName(name, sizeof(name));
      size_t L = strlen(name);
      if (L > 4 && name[L - 4] == '.' && (name[L - 3] | 0x20) == 'b' && (name[L - 2] | 0x20) == 'i' && (name[L - 1] | 0x20) == 'n') {
        Serial.print("F ");
        Serial.print(name);
        Serial.print(' ');
        Serial.println((unsigned long)f.fileSize());
        n++;
      }
    }
    f.close();
  }
  dir.close();
  return n;
}

#endif  // USE_SD
