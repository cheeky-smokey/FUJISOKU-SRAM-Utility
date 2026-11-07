// 表示 (20 文字 x 4 行)。キャラクタ LCD (I2C、オプション) と Web アプリに同じ内容を出す
//
// 画面の内容はいつも lcd_shadow[] に持っておき、LCD があればそこへ書き、PC には "LCD <16進>" で送る。
//  - USE_LCD = 0          : LCD のコードを入れない (LiquidCrystal I2C ライブラリも不要)
//  - USE_LCD = 1、LCD なし : 起動時に I2C アドレスへ応答がなければ、LCD には何も送らない
//
//  待機画面                 進捗画面                 結果画面
//  +--------------------+   +--------------------+   +--------------------+
//  |FUJISOKU SRAM   v2.3|   |BACKUP         00:04|   |v BACKUP        DONE|
//  |SD:OK       PC:OK   |   |Reading card        |   |32768 bytes ->      |
//  |Ready               |   |#######-------  47% |   |fujisoku.bin        |
//  |Last:v BACKUP OK    |   |0x3B00/0x8000 3.2k/s|   |Time 00:10          |
//  +--------------------+   +--------------------+   +--------------------+
//
// - 進捗バーは CGRAM の自作文字で 1 文字を 5 段階に分け、15 文字 = 75 段階で表示する
// - I2C の LCD は 1 文字の転送に約 1ms かかるため、前回の表示と違う文字だけを書き換える
// - 結果画面は RESULT_HOLD_MS 後に待機画面へ戻る (次の処理が始まればすぐ切り替わる)
// - 文字は ASCII のみ (LCD の文字 ROM の違いに左右されないように)
// - PC から "lcd on" を受けると、表示が変わるたびに "LCD <16進>" を送る (Web アプリの LCD 表示用)

#define SCREEN_NONE 0
#define SCREEN_IDLE 1
#define SCREEN_PROGRESS 2
#define SCREEN_RESULT 3

// 自作文字の文字コード。0x08-0x0F は CGRAM 0-7 と同じ文字になる (文字列に 0 を入れずに済む)
#define CH_BAR(k) ((char)(0x08 + (k)-1))  // k = 1..5 列ぶん塗ったバー
#define CH_RAIL ((char)0x0D)              // バーの空き部分
#define CH_OK ((char)0x0E)                // チェック
#define CH_NG ((char)0x0F)                // バツ

#define BAR_CHARS 15

static char lcd_shadow[LCD_ROWS][LCD_COLS];
static bool lcd_present = false;  // LCD が見つかった (USE_LCD = 0 なら常に false)
static uint8_t ui_screen = SCREEN_NONE;
static char ui_title[16];
static char ui_sub[21];
static uint32_t ui_total = 1;
static uint32_t ui_done = 0;
static uint32_t ui_t0 = 0;
static uint32_t ui_last_draw = 0;
static uint32_t ui_result_ms = 0;
static char ui_last_text[21] = "";
static uint8_t ui_last_res = 255;
static uint32_t ui_idle_ms = 0;
static bool lcd_dirty = false;      // 前回 PC に送ってから表示が変わった
static bool lcd_push = false;       // 表示が変わったら PC に送る (lcd on)
static uint32_t lcd_push_ms = 0;

// ---------------------------------------------------------------------
// 描画の部品
// ---------------------------------------------------------------------

// r 行目を s で置き換える (20 文字に満たない分は空白)。変わった文字だけ送る
static void lcd_row(uint8_t r, const char *s) {
  char line[LCD_COLS];
  size_t n = strlen(s);
  for (uint8_t c = 0; c < LCD_COLS; c++) line[c] = (c < n) ? s[c] : ' ';
  int8_t cur = -1;  // カーソルの位置 (-1 = 不明)
  for (uint8_t c = 0; c < LCD_COLS; c++) {
    if (line[c] == lcd_shadow[r][c]) continue;
#if USE_LCD
    if (lcd_present) {
      if (cur != (int8_t)c) lcd.setCursor(c, r);
      lcd.write((uint8_t)line[c]);
    }
#endif
    lcd_shadow[r][c] = line[c];
    cur = (int8_t)(c + 1);
    lcd_dirty = true;
  }
  (void)cur;
}

bool ui_lcd_present() {
  return lcd_present;
}

// LCD の 80 文字を "LCD <16進 160 桁>" で PC に送る (Web アプリが同じ画面を描く)
void ui_dump_lcd() {
  static const char hx[] = "0123456789ABCDEF";
  char line[4 + LCD_ROWS * LCD_COLS * 2 + 2];
  int p = 0;
  memcpy(line, "LCD ", 4);
  p = 4;
  for (uint8_t r = 0; r < LCD_ROWS; r++)
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      uint8_t b = (uint8_t)lcd_shadow[r][c];
      line[p++] = hx[b >> 4];
      line[p++] = hx[b & 15];
    }
  line[p++] = '\n';
  Serial.write((const uint8_t *)line, p);
  lcd_dirty = false;
  lcd_push_ms = millis();
}

// lcd on / off: 表示が変わるたびに送るかどうか。USB が切れたら自動的にやめる
void ui_set_lcd_push(bool on) {
  lcd_push = on;
  if (on) ui_dump_lcd();
}

static void ui_push_if_changed() {
  if (!lcd_push) return;
  if (!Serial) {
    lcd_push = false;
    return;
  }
  if (lcd_dirty && millis() - lcd_push_ms >= 250) ui_dump_lcd();
}

static void lcd_clear_all() {
#if USE_LCD
  if (lcd_present) lcd.clear();
#endif
  memset(lcd_shadow, ' ', sizeof(lcd_shadow));
}

static void fmt_time(char *out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s > 5999) s = 5999;
  snprintf(out, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

#if USE_LCD
// 自作文字を登録する (ライブラリの createChar は uint8_t[] を受け取るので const にしない)
static void lcd_make_glyphs() {
  uint8_t g[8];
  for (uint8_t k = 1; k <= 5; k++) {
    uint8_t fill = (uint8_t)((0x1F << (5 - k)) & 0x1F);
    for (uint8_t r = 0; r < 8; r++) g[r] = (r >= 1 && r <= 6) ? fill : 0;
    g[6] = 0x1F;  // 下の線はつねに引く (空き部分の線とつながる)
    lcd.createChar(k - 1, g);
  }
  for (uint8_t r = 0; r < 8; r++) g[r] = 0;
  g[6] = 0x1F;
  lcd.createChar(5, g);  // 空き部分
  uint8_t ok[8] = { 0x00, 0x01, 0x03, 0x16, 0x1C, 0x08, 0x00, 0x00 };
  lcd.createChar(6, ok);
  uint8_t ng[8] = { 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00 };
  lcd.createChar(7, ng);
}
#endif

static char res_icon(uint8_t res) {
  if (res == RES_OK) return CH_OK;
  if (res == RES_CANCEL) return '-';
  if (res == RES_NG) return '!';
  return CH_NG;
}

// ---------------------------------------------------------------------
// 起動・待機画面
// ---------------------------------------------------------------------
void ui_begin() {
#if USE_LCD
  // LCD がつながっているか確かめる (無ければ以後 LCD には何も送らない。R4 の Wire は応答がなくても止まらない)
  Wire.begin();
  Wire.beginTransmission(LCD_I2C_ADDR);
  lcd_present = (Wire.endTransmission() == 0);
  if (lcd_present) {
    lcd.init();
    lcd.backlight();
    lcd_make_glyphs();
  }
#endif
  lcd_clear_all();  // createChar の後はアドレスが CGRAM を指したままなので、clear で表示側に戻す
  lcd_row(0, "FUJISOKU SRAM");
  lcd_row(1, "Utility  v" FSU_VERSION);
  lcd_row(3, "Starting...");
}

void ui_splash_status(const char *text) {
  lcd_row(3, text);
}

static void draw_idle_status() {
  char line[40];
  bool usb = (bool)Serial;
#if USE_SD
  const char *sd = sd_state == SD_STATE_READY ? "OK" : (sd_state == SD_STATE_ABSENT ? "NONE" : "--");
#else
  const char *sd = "off";  // SD カードを使わない設定
#endif
  snprintf(line, sizeof(line), "SD:%-8sPC:%s", sd, usb ? "OK" : "--");
  lcd_row(1, line);
  lcd_row(2, usb ? "Ready" : "Waiting for PC (USB)");
}

static void draw_idle() {
  char line[40];
  snprintf(line, sizeof(line), "%-15s%5s", "FUJISOKU SRAM", "v" FSU_VERSION);
  lcd_row(0, line);
  draw_idle_status();
  if (ui_last_res == 255) {
    lcd_row(3, "Last:---");
  } else {
    snprintf(line, sizeof(line), "Last:%s", ui_last_text);
    lcd_row(3, line);
  }
  ui_screen = SCREEN_IDLE;
}

void ui_show_idle() {
  draw_idle();
}

// ---------------------------------------------------------------------
// 進捗画面
// ---------------------------------------------------------------------
static void draw_progress_bar() {
  char line[40];
  uint32_t total = ui_total ? ui_total : 1;
  uint32_t done = ui_done > total ? total : ui_done;
  uint32_t units = (uint32_t)((uint64_t)done * (BAR_CHARS * 5) / total);
  uint32_t pct = (uint32_t)((uint64_t)done * 100 / total);
  for (uint8_t i = 0; i < BAR_CHARS; i++) {
    uint32_t lit = units > (uint32_t)i * 5 ? units - (uint32_t)i * 5 : 0;
    line[i] = lit == 0 ? CH_RAIL : CH_BAR(lit >= 5 ? 5 : lit);
  }
  snprintf(line + BAR_CHARS, sizeof(line) - BAR_CHARS, " %3lu%%", (unsigned long)pct);
  lcd_row(2, line);
}

static void draw_progress_info(uint32_t now) {
  char line[40], t[8];
  fmt_time(t, sizeof(t), now - ui_t0);
  snprintf(line, sizeof(line), "%-15.15s%5s", ui_title, t);
  lcd_row(0, line);
  uint32_t done = ui_done > ui_total ? ui_total : ui_done;
  if (ui_total > 100) {
    char rate[16] = "";
    uint32_t el = now - ui_t0;
    if (el >= 1000) {
      uint32_t bps = (uint32_t)((uint64_t)done * 1000 / el);
      if (bps >= 1000) snprintf(rate, sizeof(rate), "%lu.%luk/s", (unsigned long)(bps / 1000), (unsigned long)(bps % 1000 / 100));
      else snprintf(rate, sizeof(rate), "%luB/s", (unsigned long)bps);
    }
    char addr[16];
    snprintf(addr, sizeof(addr), "0x%04lX/0x%04lX", (unsigned long)(done & 0xFFFF), (unsigned long)(ui_total & 0xFFFF));
    if (ui_total > 0xFFFF) snprintf(addr, sizeof(addr), "%lu/%lu", (unsigned long)(done / 1024), (unsigned long)(ui_total / 1024));
    snprintf(line, sizeof(line), "%-13.13s%7s", addr, rate);
  } else {
    snprintf(line, sizeof(line), "%lu/%lu", (unsigned long)done, (unsigned long)ui_total);
  }
  lcd_row(3, line);
}

// 進捗画面を出す。title: 1 行目の処理名、sub: 段階が届くまで 2 行目に出す説明、total: 進捗の合計
void ui_progress_begin(const char *title, const char *sub, uint32_t total) {
  strncpy(ui_title, title, sizeof(ui_title) - 1);
  ui_title[sizeof(ui_title) - 1] = '\0';
  strncpy(ui_sub, sub, sizeof(ui_sub) - 1);
  ui_sub[sizeof(ui_sub) - 1] = '\0';
  ui_total = total ? total : 1;
  ui_done = 0;
  ui_t0 = millis();
  ui_last_draw = ui_t0;
  ui_screen = SCREEN_PROGRESS;
  lcd_row(1, ui_sub);  // 段階が届くまでは処理の内容を出す
  draw_progress_bar();
  draw_progress_info(ui_t0);
}

void ui_progress_phase(const char *text) {
  if (ui_screen != SCREEN_PROGRESS) return;
  lcd_row(1, text);
}

void ui_progress_set(uint32_t done) {
  ui_done = done;
  if (ui_screen != SCREEN_PROGRESS) return;
  draw_progress_bar();  // 変化した文字だけ送るので毎回呼んでよい
  uint32_t now = millis();
  if (now - ui_last_draw >= 500 || done >= ui_total) {
    ui_last_draw = now;
    draw_progress_info(now);
  }
  ui_push_if_changed();
}

// ---------------------------------------------------------------------
// 結果画面
// ---------------------------------------------------------------------

// 20 文字で 2 行に折り返す (できるだけ空白で切る)
static void wrap2(const char *s, char *l1, char *l2) {
  size_t n = strlen(s);
  if (n <= LCD_COLS) {
    memcpy(l1, s, n + 1);
    l2[0] = '\0';
    return;
  }
  size_t cut = LCD_COLS;
  while (cut > 8 && s[cut] != ' ') cut--;
  if (s[cut] != ' ') cut = LCD_COLS;
  memcpy(l1, s, cut);
  l1[cut] = '\0';
  const char *rest = s + cut + (s[cut] == ' ' ? 1 : 0);
  strncpy(l2, rest, LCD_COLS);
  l2[LCD_COLS] = '\0';
}

// 結果の見出しと、利用者向けの説明
static const char *result_head(uint8_t res, const char *code, const char *msg, const char **hint) {
  *hint = msg;
  if (res == RES_OK) return "DONE";
  if (res == RES_CANCEL) return "CANCELED";
  if (strcmp(code, "BADSIZE") == 0) return "BAD FILE";
  if (res == RES_NG) return "MISMATCH";
  if (strcmp(code, "NOSD") == 0) {
    *hint = "Insert an SD card and try again";
    return "NO SD";
  }
  if (strcmp(code, "SDLOST") == 0) {
    *hint = "SD card was removed. Insert it, retry";
    return "SD LOST";
  }
  if (strcmp(code, "NOFILE") == 0) return "NO FILE";
  if (strcmp(code, "SDERR") == 0) return "SD ERROR";
  if (strcmp(code, "LOST") == 0) {
    *hint = "Lost contact with PC";
    return "PC LOST";
  }
  return "ERROR";
}

void ui_progress_end(uint8_t res, const char *code, const char *msg) {
  char line[40], l1[24], l2[24], t[8];
  const char *hint;
  if (code == NULL) code = "";
  if (msg == NULL) msg = "";
  const char *head = result_head(res, code, msg, &hint);
  uint32_t now = millis();

  // 待機画面の「Last:」用
  const char *word = res == RES_OK ? "OK" : (res == RES_NG ? "NG" : (res == RES_CANCEL ? "STOP" : "ERR"));
  int lw = LCD_COLS - 5 - 2 - 1 - (int)strlen(word);  // "Last:" + アイコンと空白 + 空白 + 結果
  snprintf(ui_last_text, sizeof(ui_last_text), "%c %.*s %s", res_icon(res), lw, ui_title, word);
  ui_last_res = res;

  // 1 行目: アイコン 処理名 .... 見出し
  int tw = LCD_COLS - 3 - (int)strlen(head);
  if (tw < 0) tw = 0;
  snprintf(line, sizeof(line), "%c %-*.*s %s", res_icon(res), tw, tw, ui_title, head);
  lcd_row(0, line);
  wrap2(hint, l1, l2);
  lcd_row(1, l1);
  lcd_row(2, l2);
  fmt_time(t, sizeof(t), now - ui_t0);
  if (res == RES_OK || code[0] == '\0' || strcmp(code, "PC") == 0) snprintf(line, sizeof(line), "Time %s", t);
  else snprintf(line, sizeof(line), "Time %s [%.8s]", t, code);
  lcd_row(3, line);
  ui_screen = SCREEN_RESULT;
  ui_result_ms = now;
}

// ---------------------------------------------------------------------
// 表示の更新 (loop から繰り返し呼ぶ)
// ---------------------------------------------------------------------
void ui_poll() {
  uint32_t now = millis();
  ui_push_if_changed();
  if (ui_screen == SCREEN_RESULT) {
    if (op_session == 0 && now - ui_result_ms > RESULT_HOLD_MS) draw_idle();
    return;
  }
  if (ui_screen == SCREEN_PROGRESS) {
    // PC 主導の表示では、PC からの更新の合間も経過時間を進める
    if (op_session == 2 && now - ui_last_draw >= 500) {
      ui_last_draw = now;
      draw_progress_info(now);
    }
    return;
  }
  if (ui_screen == SCREEN_IDLE && now - ui_idle_ms >= 200) {
    ui_idle_ms = now;
    draw_idle_status();  // SD の抜き差しや USB の接続を反映 (変わった文字だけ送る)
  }
}
