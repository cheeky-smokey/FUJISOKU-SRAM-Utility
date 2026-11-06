// PC (Web アプリ / シリアルモニタ) とのやりとり
//
// 1 行 1 コマンド (改行 \n で終わる)。コマンド名は大文字・小文字を区別しない。数値は 10 進、または 0x 付きの 16 進。
// 1 文字だけの行は従来のコマンド (R C X S W B L V T E e) として扱う。
// 詳細は docs/protocol.md を参照。
//
//   hello / status / sdcheck / help
//   rd <f|s|p> <addr> <len>          -> D <addr(hex)> <hex...> ... , OK rd <len>
//   wr <f|s|p> <addr> <hex...>       -> OK wr <addr> <n>
//   fill <f|s|p> <addr> <len> <byte> -> OK fill <len>
//   copy <x> <y> / cmp [<x> <y>] / test [s|p] / erase <x>   (p は USE_SRAM62256 のときだけ)
//   sd save|load|verify <f|s|p> [name|-] [v] / sd ls / sd check
//   lcd / lcd on / lcd off
//   ui begin <total> <title> / ui prog <done> [phase] / ui end <ok|ng|err|cancel> [text]
//   '!' (改行なし) : 処理の中断

#define RX_MAX 200
#define PC_SESSION_TIMEOUT_MS 8000

static char rx_line[RX_MAX];
static uint16_t rx_len = 0;
static bool rx_over = false;
static const char HEXCHARS[] = "0123456789ABCDEF";

void reply_err(const char *code, const char *msg) {
  Serial.print("ERR ");
  Serial.print(code);
  Serial.print(' ');
  Serial.println(msg);
}

// "0x" 付きなら 16 進、なければ 10 進。数値でなければ false
static bool parse_num(const char *s, uint32_t *out) {
  if (s == NULL || s[0] == '\0') return false;
  char *end = NULL;
  int base = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
  unsigned long v = strtoul(s, &end, base);
  if (end == s || *end != '\0') return false;
  *out = (uint32_t)v;
  return true;
}

static bool parse_dev(const char *s, uint8_t *dev) {
  if (s == NULL || s[1] != '\0') return false;
  char c = (char)(s[0] | 0x20);
  if (c == 'f') *dev = DEV_FUJISOKU;
  else if (c == 's') *dev = DEV_SPI_SRAM;
#if USE_SRAM62256
  else if (c == 'p') *dev = DEV_SRAM62256;
#endif
  else return false;
  return true;
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// '_' を空白に変える (LCD 用の短い文章を 1 トークンで渡すため)
static void underscores_to_spaces(char *s) {
  for (; *s; s++)
    if (*s == '_') *s = ' ';
}

void print_status() {
  Serial.print("STATUS sd=");
  Serial.print(sd_state == SD_STATE_READY ? 1 : 0);
  Serial.print(" busy=");
  Serial.print(op_session ? 1 : 0);
  Serial.print(" spi=");
  Serial.print(spi_sram_rdmr(0), HEX);
  Serial.print(" lcd=");
  Serial.println(ui_lcd_present() ? 1 : 0);
}

void print_help() {
  Serial.println("FUJISOKU SRAM Utility " FSU_VERSION " - send commands with a newline");
  Serial.println("  R card->SPI SRAM   C SPI SRAM->card   X compare   T test SPI SRAM");
  Serial.println("  B card->SD         L SD->card         V verify    S SPI SRAM->SD   W SD->SPI SRAM");
  Serial.println("  E erase SPI SRAM   e erase card       ! cancel");
  Serial.println("  hello status sdcheck rd wr fill copy cmp test erase sd ui lcd (see docs/protocol.md)");
  Serial.print("  targets: f = card, s = SPI SRAM");
  Serial.println(USE_SRAM62256 ? ", p = 62256" : "");
}

// ---------------------------------------------------------------------
// rd / wr / fill
// ---------------------------------------------------------------------

// 中断要求 (PC の '!') を確認して消費する
static bool proto_cancel_taken() {
  if (op_cancel) {
    op_cancel = false;
    return true;
  }
  return false;
}

static void cmd_rd(uint8_t dev, uint32_t addr, uint32_t len) {
  char line[24 + CHUNK_BYTES * 2];
  if (len == 0 || addr + len > CARD_SIZE) {
    reply_err("RANGE", "out of range");
    return;
  }
  uint32_t a = addr;
  uint32_t end = addr + len;
  while (a < end) {
    uint32_t n = end - a;
    if (n > CHUNK_BYTES) n = CHUNK_BYTES;
    int p = snprintf(line, sizeof(line), "D %04lX ", (unsigned long)a);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t b = dev_read(dev, a + i);
      line[p++] = HEXCHARS[b >> 4];
      line[p++] = HEXCHARS[b & 15];
    }
    line[p++] = '\n';
    Serial.write((const uint8_t *)line, p);
    a += n;
    op_session_ms = millis();
    op_poll_cancel();
    if (proto_cancel_taken()) {
      bus_idle();
      reply_err("CANCEL", "cancelled");
      return;
    }
  }
  bus_idle();
  Serial.print("OK rd ");
  Serial.println((unsigned long)len);
}

static void cmd_wr(uint8_t dev, uint32_t addr, const char *hex) {
  size_t hl = strlen(hex);
  if (hl == 0 || (hl & 1) || hl / 2 > CHUNK_BYTES) {
    reply_err("ARG", "bad hex length");
    return;
  }
  uint32_t n = hl / 2;
  if (addr + n > CARD_SIZE) {
    reply_err("RANGE", "out of range");
    return;
  }
  for (uint32_t i = 0; i < n; i++) {
    int h = hex_val(hex[i * 2]);
    int l = hex_val(hex[i * 2 + 1]);
    if (h < 0 || l < 0) {
      reply_err("ARG", "bad hex digit");
      return;
    }
    blockbuf[i] = (uint8_t)(h * 16 + l);
  }
  if (proto_cancel_taken()) {
    reply_err("CANCEL", "cancelled");
    return;
  }
  for (uint32_t i = 0; i < n; i++) dev_write(dev, addr + i, blockbuf[i]);
  bus_idle();
  Serial.print("OK wr ");
  Serial.print((unsigned long)addr);
  Serial.print(' ');
  Serial.println((unsigned long)n);
}

static void cmd_fill(uint8_t dev, uint32_t addr, uint32_t len, uint8_t value) {
  if (len == 0 || addr + len > CARD_SIZE) {
    reply_err("RANGE", "out of range");
    return;
  }
  for (uint32_t i = 0; i < len; i++) {
    dev_write(dev, addr + i, value);
    if ((i & 0xFF) == 0xFF) {
      op_session_ms = millis();
      op_poll_cancel();
      if (proto_cancel_taken()) {
        bus_idle();
        reply_err("CANCEL", "cancelled");
        return;
      }
    }
  }
  bus_idle();
  Serial.print("OK fill ");
  Serial.println((unsigned long)len);
}

// ---------------------------------------------------------------------
// ui (PC 主導の処理を LCD に表示する)
// ---------------------------------------------------------------------
static void cmd_ui(int argc, char **argv) {
  if (argc < 2) {
    reply_err("ARG", "ui begin|prog|end");
    return;
  }
  if (strcmp(argv[1], "begin") == 0) {
    uint32_t total;
    if (argc < 4 || !parse_num(argv[2], &total) || total == 0) {
      reply_err("ARG", "ui begin <total> <title>");
      return;
    }
    if (op_session == 1) {
      reply_err("BUSY", "busy");
      return;
    }
    underscores_to_spaces(argv[3]);
    op_cancel = false;
    op_session = 2;
    op_session_ms = millis();
    ui_progress_begin(argv[3], "from PC (USB)", total, true);
    Serial.println("OK ui");
  } else if (strcmp(argv[1], "prog") == 0) {
    uint32_t done;
    if (op_session != 2 || argc < 3 || !parse_num(argv[2], &done)) {
      reply_err("ARG", "ui prog <done> [phase]");
      return;
    }
    if (argc >= 4) {
      underscores_to_spaces(argv[3]);
      ui_progress_phase(argv[3]);
    }
    ui_progress_set(done);
    if (proto_cancel_taken()) {
      reply_err("CANCEL", "cancelled");
      return;
    }
    Serial.println("OK ui");
  } else if (strcmp(argv[1], "end") == 0) {
    if (op_session != 2 || argc < 3) {
      reply_err("ARG", "ui end <ok|ng|err|cancel> [text]");
      return;
    }
    uint8_t res = RES_ERR;
    if (strcmp(argv[2], "ok") == 0) res = RES_OK;
    else if (strcmp(argv[2], "ng") == 0) res = RES_NG;
    else if (strcmp(argv[2], "cancel") == 0) res = RES_CANCEL;
    char text[40] = "";
    if (argc >= 4) {
      underscores_to_spaces(argv[3]);
      strncpy(text, argv[3], sizeof(text) - 1);
      text[sizeof(text) - 1] = '\0';
    }
    ui_progress_end(res, "PC", text);
    op_session = 0;
    op_cancel = false;
    Serial.println("OK ui");
  } else {
    reply_err("ARG", "ui begin|prog|end");
  }
}

// ---------------------------------------------------------------------
// コマンドの振り分け
// ---------------------------------------------------------------------

// 処理を始められるか (他の処理中は BUSY)
static bool proto_idle_or_busy() {
  if (op_session) {
    reply_err("BUSY", "busy");
    return false;
  }
  return true;
}

// 従来の 1 文字コマンド
static void proto_legacy(char c) {
  if (c != '?' && c != 'h' && !proto_idle_or_busy()) return;
  switch (c) {
    case 'R': op_copy(DEV_FUJISOKU, DEV_SPI_SRAM); break;
    case 'C': op_copy(DEV_SPI_SRAM, DEV_FUJISOKU); break;
    case 'X': op_compare(DEV_SPI_SRAM, DEV_FUJISOKU); break;
    case 'S': op_sd_save(DEV_SPI_SRAM, NULL); break;
    case 'W': op_sd_load(DEV_SPI_SRAM, NULL, false); break;
    case 'B': op_sd_save(DEV_FUJISOKU, NULL); break;
    case 'L': op_sd_load(DEV_FUJISOKU, NULL, false); break;
    case 'V': op_sd_verify(DEV_FUJISOKU, NULL); break;
    case 'T': op_test_spi(); break;
    case 'E': op_erase(DEV_SPI_SRAM); break;
    case 'e': op_erase(DEV_FUJISOKU); break;
    case '?':
    case 'h': print_help(); break;
    default: reply_err("CMD", "unknown command"); break;
  }
}

static void proto_sd(int argc, char **argv) {
  uint8_t dev = DEV_FUJISOKU;
  const char *name = NULL;
  if (argc < 2) {
    reply_err("ARG", "sd save|load|verify|ls|check");
    return;
  }
  if (strcmp(argv[1], "ls") == 0) {
    if (proto_idle_or_busy()) op_sd_list();
    return;
  }
  if (strcmp(argv[1], "check") == 0) {
    if (proto_idle_or_busy()) op_sd_check();
    return;
  }
  if (argc < 3 || !parse_dev(argv[2], &dev)) {
    reply_err("ARG", "sd <save|load|verify> <f|s|p> [name|-] [v]");
    return;
  }
  if (argc >= 4 && strcmp(argv[3], "-") != 0) name = argv[3];
  bool verify_after = (argc >= 5 && argv[4][0] == 'v');
  if (!proto_idle_or_busy()) return;
  if (strcmp(argv[1], "save") == 0) op_sd_save(dev, name);
  else if (strcmp(argv[1], "load") == 0) op_sd_load(dev, name, verify_after);
  else if (strcmp(argv[1], "verify") == 0) op_sd_verify(dev, name);
  else reply_err("ARG", "sd save|load|verify|ls|check");
}

void proto_exec(char *line) {
  char *argv[8];
  int argc = 0;
  uint8_t dev = DEV_FUJISOKU, dev2 = DEV_FUJISOKU;
  uint32_t v1 = 0, v2 = 0, v3 = 0;

  if (op_session == 2) op_session_ms = millis();

  // 1 文字だけの行 = 従来コマンド
  if (line[0] != '\0' && line[1] == '\0') {
    proto_legacy(line[0]);
    return;
  }

  // 空白で分割
  char *p = line;
  while (*p && argc < 8) {
    while (*p == ' ' || *p == '\t') *p++ = '\0';
    if (*p == '\0') break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
  }
  if (argc == 0) return;
  for (char *c = argv[0]; *c; c++)
    if (*c >= 'A' && *c <= 'Z') *c += 32;  // コマンド名は大文字小文字を区別しない
  const char *cmd = argv[0];

  if (strcmp(cmd, "hello") == 0) {
    Serial.print("HELLO FSU " FSU_VERSION " size=");
    Serial.print((unsigned long)CARD_SIZE);
    Serial.print(" chunk=");
    Serial.print(CHUNK_BYTES);
    Serial.print(" lcd=");
    Serial.print(ui_lcd_present() ? "20x4" : "none");
    Serial.print(" sram62256=");
    Serial.println(USE_SRAM62256);
  } else if (strcmp(cmd, "status") == 0) {
    print_status();
  } else if (strcmp(cmd, "sdcheck") == 0) {
    if (proto_idle_or_busy()) op_sd_check();
  } else if (strcmp(cmd, "help") == 0) {
    print_help();
  } else if (strcmp(cmd, "rd") == 0) {
    if (argc < 4 || !parse_dev(argv[1], &dev) || !parse_num(argv[2], &v1) || !parse_num(argv[3], &v2)) {
      reply_err("ARG", "rd <f|s|p> <addr> <len>");
    } else if (op_session == 1) {
      reply_err("BUSY", "busy");
    } else {
      cmd_rd(dev, v1, v2);
    }
  } else if (strcmp(cmd, "wr") == 0) {
    if (argc < 4 || !parse_dev(argv[1], &dev) || !parse_num(argv[2], &v1)) {
      reply_err("ARG", "wr <f|s|p> <addr> <hex>");
    } else if (op_session == 1) {
      reply_err("BUSY", "busy");
    } else {
      cmd_wr(dev, v1, argv[3]);
    }
  } else if (strcmp(cmd, "fill") == 0) {
    if (argc < 5 || !parse_dev(argv[1], &dev) || !parse_num(argv[2], &v1) || !parse_num(argv[3], &v2) || !parse_num(argv[4], &v3) || v3 > 255) {
      reply_err("ARG", "fill <f|s|p> <addr> <len> <byte>");
    } else if (op_session == 1) {
      reply_err("BUSY", "busy");
    } else {
      cmd_fill(dev, v1, v2, (uint8_t)v3);
    }
  } else if (strcmp(cmd, "copy") == 0) {
    if (argc < 3 || !parse_dev(argv[1], &dev) || !parse_dev(argv[2], &dev2) || dev == dev2) {
      reply_err("ARG", "copy <f|s|p> <f|s|p>");
    } else if (proto_idle_or_busy()) {
      op_copy(dev, dev2);
    }
  } else if (strcmp(cmd, "cmp") == 0) {
    // cmp = cmp s f (SPI SRAM とカード)。cmp <x> <y> で任意の 2 つ
    dev = DEV_SPI_SRAM;
    dev2 = DEV_FUJISOKU;
    if (argc >= 2 && (argc < 3 || !parse_dev(argv[1], &dev) || !parse_dev(argv[2], &dev2) || dev == dev2)) {
      reply_err("ARG", "cmp [<f|s|p> <f|s|p>]");
    } else if (proto_idle_or_busy()) {
      op_compare(dev, dev2);
    }
  } else if (strcmp(cmd, "test") == 0) {
    // test / test s = SPI SRAM の自己診断、test p = 62256 でバスの配線試験
    if (argc >= 2 && (!parse_dev(argv[1], &dev) || dev == DEV_FUJISOKU)) {
      reply_err("ARG", "test [s|p]");
    } else if (!proto_idle_or_busy()) {
    } else if (argc >= 2 && dev == DEV_SRAM62256) {
#if USE_SRAM62256
      op_test_62256();
#endif
    } else {
      op_test_spi();
    }
  } else if (strcmp(cmd, "erase") == 0) {
    if (argc < 2 || !parse_dev(argv[1], &dev)) {
      reply_err("ARG", "erase <f|s|p>");
    } else if (proto_idle_or_busy()) {
      op_erase(dev);
    }
  } else if (strcmp(cmd, "lcd") == 0) {
    // lcd: 今の表示を 1 回送る。lcd on / off: 表示が変わるたびに送る / やめる
    if (argc >= 2 && strcmp(argv[1], "on") == 0) ui_set_lcd_push(true);
    else if (argc >= 2 && strcmp(argv[1], "off") == 0) {
      ui_set_lcd_push(false);
      Serial.println("OK lcd off");
    } else ui_dump_lcd();
  } else if (strcmp(cmd, "sd") == 0) {
    proto_sd(argc, argv);
  } else if (strcmp(cmd, "ui") == 0) {
    cmd_ui(argc, argv);
  } else {
    reply_err("CMD", "unknown command");
  }
}

// シリアルから 1 行ずつ取り込んで実行する。loop() から繰り返し呼ぶ
void proto_poll() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '!') continue;  // 中断の合図。処理中は op_poll_cancel が受け取る。待機中に届いても捨てる
    if (c == '\n') {
      if (rx_over) {
        reply_err("LINE", "line too long");
      } else if (rx_len > 0) {
        rx_line[rx_len] = '\0';
        proto_exec(rx_line);
      }
      rx_len = 0;
      rx_over = false;
      continue;
    }
    if (rx_len < RX_MAX - 1) rx_line[rx_len++] = (char)c;
    else rx_over = true;
  }
  // PC 主導の処理中に PC からの連絡が途絶えたら画面を戻す
  if (op_session == 2 && millis() - op_session_ms > PC_SESSION_TIMEOUT_MS) {
    ui_progress_end(RES_ERR, "LOST", "PC connection lost");
    op_session = 0;
    op_cancel = false;
    Serial.println("E UI timeout");
  }
}
