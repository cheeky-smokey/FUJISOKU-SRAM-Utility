// FUJISOKU SRAM Utility 用ホストシミュレータ
//
// スケッチ (FUJISOKU_SRAM_COPY/*.ino) をそのままホストでビルドして動かすための土台。
//   - 仮想時計 (delay は時間を進めるだけ。SPI 1 バイトごとにも進む)
//   - MCP23S17 ×8 (IOCON の BANK/HAEN、BANK0/1 のレジスタ配置、A2 ピンの癖まで再現)
//   - FUJISOKU カード (32KB) / 並列 62256 / SPI SRAM 23LC512 を、物理的な配線から再現
//       ch7 GPIOB: B0 = CS2 (= 62256 の nCS)、B1 = nOE、B2 = 62256 の nOE、B3 = nWE、B4 = nCS1、B5 = 未使用
//       SPI SRAM の /CS は Arduino の D6
//   - SD カード (ホストのディレクトリ。抜き差し・世代管理・SPI の停止まで再現)
//   - 20x4 キャラクタ LCD (HD44780 + PCF8574) のモデル。文字列の取り出しと画像化ができる
//   - シリアル = 標準入出力、制御チャンネル = fd 3 (コマンド) / fd 4 (応答)
//   - バスの不正利用を数える診断カウンタ (トランザクション外転送、MCP の上限クロック超え、
//     バス競合、書き込み中のアドレス・データ変更、SPI 停止中の転送 など)

#include "Arduino.h"
#include "SPI.h"
#include "SdFat.h"
#include "Wire.h"
#include "LiquidCrystal_I2C.h"
#include "font5x7.h"
#include "config.h"

#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

void setup();
void loop();

// ---------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------
static uint64_t vt_us = 0;
static bool g_quit = false;
static bool g_quiet = false;  // トリガー内のコマンドは応答を返さない
static bool usb_connected = true;
static bool realtime = false;  // 待機中の仮想時間を実時間に合わせる (ブラウザとつなぐテスト用)

struct Diag {
  uint32_t no_txn = 0, spi_dead = 0, over_clock = 0, bus_conflict = 0, contention = 0;
  uint32_t addr_change_in_write = 0, data_change_in_write = 0, oe_we_overlap = 0, mcp_unaddressed = 0, sd_stale = 0;
} diag;

static bool spi_sram_present = true;  // false: SPI SRAM が無い (MISO は High のまま)
static bool card_present = true;
static int glitch_addr = -1;
// バス線の故障 (H / L 固定) の再現: mask のビットが val に固定される
static uint16_t addr_stuck_mask = 0, addr_stuck_val = 0;
static uint8_t data_stuck_mask = 0, data_stuck_val = 0;  // このアドレスを次に読んだとき 1 回だけ値を化けさせる (接触不良の再現)

static uint8_t pin_level[64];

// ---------------------------------------------------------------------
// 仮想時計
// ---------------------------------------------------------------------
unsigned long millis() { return (unsigned long)(vt_us / 1000); }
unsigned long micros() { return (unsigned long)vt_us; }
static void sim_poll_io();
void delay(unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) {
    vt_us += 1000;
    if ((i & 15) == 0) sim_poll_io();
  }
  usleep((useconds_t)(ms * 30));  // 実時間でも少し待つ (タッチ補正のように外部操作を待つ処理のため)
}
void delayMicroseconds(unsigned int us) { vt_us += us; }
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---------------------------------------------------------------------
// MCP23S17
// ---------------------------------------------------------------------
struct Mcp {
  uint8_t r[2][11];  // [port][IODIR IPOL GPINTEN DEFVAL INTCON IOCON GPPU INTF INTCAP GPIO OLAT]
  bool bank1 = false, haen = false;
  int st = 0;
  bool rd = false;
  uint8_t addr = 0;
  void reset() {
    memset(r, 0, sizeof(r));
    r[0][0] = r[1][0] = 0xFF;
    bank1 = haen = false;
    st = 0;
  }
};
static Mcp mcp[8];

enum { R_IODIR = 0, R_IOCON = 5, R_GPPU = 6, R_GPIO = 9, R_OLAT = 10 };

static bool mcp_decode(const Mcp &m, uint8_t a, int *port, int *reg) {
  if (m.bank1) {
    *port = (a & 0x10) ? 1 : 0;
    *reg = a & 0x0F;
    return *reg <= 10;
  }
  if (a > 0x15) return false;
  *port = a & 1;
  *reg = a >> 1;
  return true;
}

// ピンの電圧 (出力なら OLAT、入力なら外部から来た値 / プルアップ)
static uint8_t mcp_pins_basic(int ch, int port, uint8_t ext, uint8_t ext_mask) {
  Mcp &m = mcp[ch];
  uint8_t dir = m.r[port][R_IODIR];
  uint8_t out = m.r[port][R_OLAT];
  // 入力ピンで外から駆動されていないものは H として読む
  // (基板側のプルアップがある前提。起動直後の浮きによる誤動作は再現しない)
  uint8_t in = (uint8_t)((ext & ext_mask) | (uint8_t)~ext_mask);
  return (uint8_t)((out & ~dir) | (in & dir));
}

// ch7 の制御ビットとデータバス、ch6 のアドレス
struct Sig {
  uint16_t addr;
  uint8_t data;  // MCP が出している (または浮いている) データ
  bool card_sel, card_oe, card_we, ram_sel, ram_oe, ram_we;
};

static uint8_t card_mem[0x8000];
static uint8_t ram62[0x8000];

static Sig sig_eval() {
  Sig s;
  uint8_t aL = mcp_pins_basic(6, 0, 0, 0);
  uint8_t aH = mcp_pins_basic(6, 1, 0, 0);
  s.addr = (uint16_t)(aL | ((aH & 0x7F) << 8));
  s.addr = (uint16_t)((s.addr & ~addr_stuck_mask) | (addr_stuck_val & addr_stuck_mask));
  s.data = mcp_pins_basic(7, 0, 0, 0);
  s.data = (uint8_t)((s.data & ~data_stuck_mask) | (data_stuck_val & data_stuck_mask));
  uint8_t b = mcp_pins_basic(7, 1, 0, 0);
  auto bit = [&](int n) { return (b >> n) & 1; };
  // B0 = CS2(カード) / nCS(62256) 共用、B1 = nOE(カード)、B2 = nOE(62256)、B3 = nWE 共用、B4 = nCS1(カード)、B5 = 未使用
  s.card_sel = (bit(4) == 0) && (bit(0) == 1);
  s.card_oe = bit(1) == 0;
  s.card_we = bit(3) == 0;
  s.ram_sel = bit(0) == 0;
  s.ram_oe = bit(2) == 0;
  s.ram_we = bit(3) == 0;
  return s;
}

// データバスの値 (メモリが出力していればその値)
static uint8_t data_bus_read() {
  Sig s = sig_eval();
  Mcp &m = mcp[7];
  uint8_t dir = m.r[0][R_IODIR];  // 1 = 入力
  uint8_t outmask = (uint8_t)~dir;
  uint8_t outv = m.r[0][R_OLAT];
  uint8_t pull = 0xFF;  // 駆動されていないデータバスは H
  bool drv = false;
  uint8_t memv = 0xFF;
  if (s.card_sel && s.card_oe && !s.card_we && card_present) {
    drv = true;
    memv = card_mem[s.addr & 0x7FFF];
    if (glitch_addr == (int)(s.addr & 0x7FFF)) {
      memv ^= 0x01;
      glitch_addr = -1;
    }
  }
  if (s.ram_sel && s.ram_oe && !s.ram_we) {
    if (drv) {
      diag.contention++;
      memv &= ram62[s.addr & 0x7FFF];
    } else {
      drv = true;
      memv = ram62[s.addr & 0x7FFF];
    }
  }
  if ((s.card_sel && s.card_oe && s.card_we) || (s.ram_sel && s.ram_oe && s.ram_we)) diag.oe_we_overlap++;
  uint8_t bus;
  if (outmask) {
    bus = (uint8_t)((outv & outmask) | ((drv ? memv : pull) & ~outmask));
    if (drv && ((memv ^ outv) & outmask)) diag.contention++;
  } else {
    bus = drv ? memv : pull;
  }
  return (uint8_t)((bus & ~data_stuck_mask) | (data_stuck_val & data_stuck_mask));
}

static bool sram23_sel_prev = false;
struct Sram23 {
  uint8_t mem[65536];
  uint8_t mode = 0x40;  // 電源投入時はシーケンシャル
  int st = 0, ab = 0;
  uint8_t op = 0;
  uint16_t addr = 0;
  int cnt = 0;
  void reset() { st = 0; }
  uint8_t xfer(uint8_t b) {
    switch (st) {
      case 0:
        op = b;
        cnt = 0;
        if (op == 0x03 || op == 0x02) { st = 1; ab = 0; addr = 0; }
        else if (op == 0x05) st = 5;
        else if (op == 0x01) st = 6;
        else st = 9;
        return 0xFF;
      case 1:
        addr = (uint16_t)((addr << 8) | b);
        if (++ab == 2) st = (op == 0x03) ? 3 : 4;
        return 0xFF;
      case 3: {
        uint8_t m = mode >> 6;
        if (m == 0 && cnt >= 1) return 0xFF;
        uint8_t v = mem[addr];
        cnt++;
        if (m == 2) addr = (uint16_t)((addr & ~31) | ((addr + 1) & 31));
        else addr++;
        return v;
      }
      case 4: {
        uint8_t m = mode >> 6;
        if (m == 0 && cnt >= 1) return 0xFF;
        mem[addr] = b;
        cnt++;
        if (m == 2) addr = (uint16_t)((addr & ~31) | ((addr + 1) & 31));
        else addr++;
        return 0xFF;
      }
      case 5: return mode;
      case 6: mode = b; st = 9; return 0xFF;
      default: return 0xFF;
    }
  }
};
static Sram23 sram23;

static bool sram23_selected() {
  return spi_sram_present && pin_level[PIN_CS_SPI_SRAM] == LOW;
}
static void sram23_update_select() {
  bool now = sram23_selected();
  if (now != sram23_sel_prev) sram23.reset();
  sram23_sel_prev = now;
}

static void mcp_reg_write(int ch, int port, int reg, uint8_t v) {
  Mcp &m = mcp[ch];
  bool relevant = (ch == 6 || ch == 7) && (reg == R_IODIR || reg == R_GPPU || reg == R_GPIO || reg == R_OLAT);
  Sig before{};
  if (relevant) before = sig_eval();
  switch (reg) {
    case R_IOCON:
      m.r[0][R_IOCON] = m.r[1][R_IOCON] = v;
      m.bank1 = (v & 0x80) != 0;
      m.haen = (v & 0x08) != 0;
      break;
    case R_GPIO:
      m.r[port][R_OLAT] = v;
      break;
    case 7:
    case 8:
      break;  // INTF / INTCAP は読み出し専用
    default:
      m.r[port][reg] = v;
  }
  if (relevant) {
    Sig after = sig_eval();
    bool wb = before.card_sel && before.card_we;
    bool wa = after.card_sel && after.card_we;
    if (wb && wa && before.addr != after.addr) {
      diag.addr_change_in_write++;
      if (getenv("FSU_SIM_DEBUG")) fprintf(stderr, "[sim] addr change during write: ch%d port%d reg%d v=%02X addr %04X -> %04X t=%lu\n", ch, port, reg, v, before.addr, after.addr, millis());
    }
    if (wb && !wa && card_present) card_mem[before.addr & 0x7FFF] = before.data;
    bool rb = before.ram_sel && before.ram_we;
    bool ra = after.ram_sel && after.ram_we;
    if (rb && ra && before.addr != after.addr) diag.addr_change_in_write++;
    if (rb && !ra) ram62[before.addr & 0x7FFF] = before.data;
    // /WE = L のあいだにデータ線が変わると、途中の値が一瞬書き込まれる
    if (((wb && wa) || (rb && ra)) && before.data != after.data) diag.data_change_in_write++;
  }
}

static uint8_t mcp_reg_read(int ch, int port, int reg) {
  Mcp &m = mcp[ch];
  if (reg == R_GPIO) {
    if (ch == 7 && port == 0) {
      uint8_t bus = data_bus_read();
      return bus;
    }
    return mcp_pins_basic(ch, port, 0, 0);
  }
  return m.r[port][reg];
}

static void mcp_deselect() {
  for (int i = 0; i < 8; i++) mcp[i].st = 0;
}

static uint8_t mcp_xfer(uint8_t b) {
  uint8_t ret = 0;
  bool any = false;
  for (int ch = 0; ch < 8; ch++) {
    Mcp &m = mcp[ch];
    switch (m.st) {
      case 0: {
        if ((b & 0xF0) != 0x40) break;
        int a = (b >> 1) & 7;
        bool match = m.haen ? (a == ch) : (a == (ch & 4));
        if (match) {
          m.st = 1;
          m.rd = b & 1;
          any = true;
        }
        break;
      }
      case 1:
        m.addr = b;
        m.st = 2;
        any = true;
        break;
      case 2: {
        int port, reg;
        any = true;
        if (!mcp_decode(m, m.addr, &port, &reg)) break;
        if (m.rd) {
          ret = mcp_reg_read(ch, port, reg);
        } else {
          mcp_reg_write(ch, port, reg, b);
        }
        // FSU_SIM_TRACE=1: MCP のレジスタアクセスを標準エラーに出す (docs/hardware.md §8 の表の元)
        static const bool trace = getenv("FSU_SIM_TRACE") != NULL;
        if (trace) fprintf(stderr, "[mcp] ch%d %s %02X %02X\n", ch, m.rd ? "R" : "W", m.addr, m.rd ? ret : b);
        if (!(m.r[0][R_IOCON] & 0x20)) m.addr++;  // SEQOP=0: アドレスが進む
        break;
      }
    }
  }
  if (!any) diag.mcp_unaddressed++;
  return ret;
}

// ---------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------
static bool spi_init = false;
static uint32_t spi_clk = 1000000;
static bool in_txn = false;
SPIClass SPI;

void SPIClass::begin() {
  spi_init = true;
  spi_clk = 1000000;
}
void SPIClass::end() { spi_init = false; }
void SPIClass::beginTransaction(SPISettings s) {
  if (!spi_init) begin();
  spi_clk = s.clk;
  in_txn = true;
}
void SPIClass::endTransaction() { in_txn = false; }

uint8_t SPIClass::transfer(uint8_t b) {
  if (!spi_init) {
    diag.spi_dead++;
    vt_us += 5;
    return 0xFF;
  }
  if (!in_txn) diag.no_txn++;
  vt_us += 6 + (8000000ULL / spi_clk);  // 呼び出しの負荷 + ビット時間
  bool m = pin_level[PIN_CS_MCP] == LOW;
  bool s = sram23_selected();
  bool sd = pin_level[PIN_CS_SD] == LOW;
  if ((int)m + (int)s + (int)sd > 1) diag.bus_conflict++;  // 2 つ以上の /CS が同時に Low
  if (m) {
    if (spi_clk > 10000000) diag.over_clock++;
    return mcp_xfer(b);
  }
  if (s) {
    if (spi_clk > 20000000) diag.over_clock++;
    return sram23.xfer(b);
  }
  return 0xFF;
}

void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t v) {
  if (pin >= sizeof(pin_level)) return;
  uint8_t old = pin_level[pin];
  pin_level[pin] = v ? 1 : 0;
  if (pin == PIN_CS_MCP && old == LOW && v) mcp_deselect();
  if (pin == PIN_CS_SPI_SRAM) sram23_update_select();
}
int digitalRead(uint8_t pin) { return pin < sizeof(pin_level) ? pin_level[pin] : 0; }

// ---------------------------------------------------------------------
// シリアル (標準入出力)
// ---------------------------------------------------------------------
SerialClass Serial;
static std::string rx_buf;
static size_t rx_pos = 0;
static std::string tx_buf;
static std::string line_buf;
static std::string ctl_buf;

struct Trigger {
  std::string prefix, cmd;
  long thresh;  // "P>=N" のとき N、そうでなければ -1
};
static std::vector<Trigger> triggers;

static void ctl_exec(const std::string &line);

static void tx_flush() {
  size_t off = 0;
  while (off < tx_buf.size()) {
    ssize_t n = ::write(1, tx_buf.data() + off, tx_buf.size() - off);
    if (n <= 0) break;
    off += (size_t)n;
  }
  tx_buf.clear();
}

static void run_triggers(const std::string &line) {
  for (size_t i = 0; i < triggers.size(); i++) {
    Trigger t = triggers[i];
    bool hit = false;
    if (t.thresh >= 0) {
      long done = -1, total = -1;
      if (sscanf(line.c_str(), "P %ld %ld", &done, &total) == 2 && done >= t.thresh) hit = true;
    } else if (line.compare(0, t.prefix.size(), t.prefix) == 0) {
      hit = true;
    }
    if (hit) {
      triggers.erase(triggers.begin() + (long)i);
      g_quiet = true;
      ctl_exec(t.cmd);
      g_quiet = false;
      return;
    }
  }
}

size_t SerialClass::write(uint8_t b) {
  if (b == '\n') {
    std::string l = line_buf;
    line_buf.clear();
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (usb_connected) tx_buf.push_back((char)b);
    run_triggers(l);
    return 1;
  }
  if (b != '\r') line_buf.push_back((char)b);
  if (usb_connected) {
    tx_buf.push_back((char)b);
    if (tx_buf.size() > 8192) tx_flush();
  }
  return 1;
}
size_t SerialClass::write(const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) write(buf[i]);
  return n;
}
int SerialClass::available() {
  sim_poll_io();
  return (int)(rx_buf.size() - rx_pos);
}
int SerialClass::read() {
  if (rx_pos >= rx_buf.size()) {
    sim_poll_io();
    if (rx_pos >= rx_buf.size()) return -1;
  }
  int c = (unsigned char)rx_buf[rx_pos++];
  if (rx_pos == rx_buf.size()) {
    rx_buf.clear();
    rx_pos = 0;
  }
  return c;
}
int SerialClass::peek() { return rx_pos < rx_buf.size() ? (unsigned char)rx_buf[rx_pos] : -1; }
void SerialClass::flush() { tx_flush(); }
SerialClass::operator bool() { return usb_connected; }

// ---------------------------------------------------------------------
// SD カード (ホストのディレクトリ)
// ---------------------------------------------------------------------
static std::string sd_root = "/tmp/fsu_sd";
static bool sd_present = true;
static uint32_t sd_gen = 1;  // カードを差し替えるたびに増える
static bool sd_mounted = false;
static uint32_t sd_mount_gen = 0;
static bool sd_begun = false;
static uint32_t sd_ops = 0;
static SdCard the_card;

static bool sd_alive() { return sd_present && sd_mounted && sd_mount_gen == sd_gen; }
static void sd_bus(uint32_t bytes) {
  // SdFat は SPI を高速 (ここでは 25MHz) に設定して使う。終わっても元には戻さない (R4 の endTransaction は何もしない)
  SPI.beginTransaction(SPISettings(25000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
  vt_us += 300 + (uint64_t)bytes * 8 / 25;
  sd_ops++;
}
static std::string sd_path(const char *p) {
  std::string s = p;
  while (!s.empty() && s[0] == '/') s.erase(0, 1);
  return s.empty() ? sd_root : sd_root + "/" + s;
}

uint32_t SdCard::sectorCount() {
  if (!sd_alive()) return 0;
  sd_bus(16);
  return 0xF0000;
}

bool SdFat::begin(uint8_t) {
  SPI.begin();  // SdFat 内部で SPI.begin() を呼ぶ
  sd_begun = true;
  sd_mounted = false;
  if (!sd_present) {
    vt_us += 2000000;  // カードが無いと約 2 秒待つ
    return false;
  }
  vt_us += 120000;
  sd_mounted = true;
  sd_mount_gen = sd_gen;
  return true;
}
void SdFat::end() {
  sd_mounted = false;
  if (sd_begun) {
    sd_begun = false;
    SPI.end();  // UNO R4 の SdFat は共有の SPI ごと止める
  }
}
SdCard *SdFat::card() { return &the_card; }
bool SdFat::exists(const char *path) {
  if (!sd_alive()) return false;
  sd_bus(32);
  struct stat st;
  return stat(sd_path(path).c_str(), &st) == 0;
}
bool SdFat::remove(const char *path) {
  if (!sd_alive()) return false;
  sd_bus(512);
  return ::unlink(sd_path(path).c_str()) == 0;
}

bool SdFat::rename(const char *oldPath, const char *newPath) {
  if (!sd_alive()) return false;
  sd_bus(512);
  std::string to = sd_path(newPath);
  struct stat st;
  if (stat(to.c_str(), &st) == 0) return false;  // FAT は既存の名前への付け替えができない
  return ::rename(sd_path(oldPath).c_str(), to.c_str()) == 0;
}

bool FsFile::open(const char *path, int oflag) {
  if (open_) close();
  if (!sd_alive()) {
    if (sd_present) diag.sd_stale++;
    return false;
  }
  sd_bus(64);
  std::string p = sd_path(path);
  struct stat st;
  if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    DIR *d = opendir(p.c_str());
    if (!d) return false;
    dirp_ = d;
    isdir_ = true;
    open_ = true;
    gen_ = sd_gen;
    return true;
  }
  int fl = O_RDONLY;
  if ((oflag & O_ACCMODE) == O_WRONLY) fl = O_WRONLY;
  if (oflag & O_CREAT) fl |= O_CREAT;
  if (oflag & O_TRUNC) fl |= O_TRUNC;
  int fd = ::open(p.c_str(), fl, 0644);
  if (fd < 0) return false;
  fd_ = fd;
  flags_ = oflag;
  isdir_ = false;
  open_ = true;
  gen_ = sd_gen;
  name_ = path;
  return true;
}

bool FsFile::openNext(FsFile *dir, int) {
  if (open_) close();
  if (!dir || !dir->open_ || !dir->isdir_ || !sd_alive() || dir->gen_ != sd_gen) return false;
  sd_bus(32);
  for (;;) {
    struct dirent *e = readdir((DIR *)dir->dirp_);
    if (!e) return false;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    std::string p = sd_root + "/" + e->d_name;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) continue;
    name_ = e->d_name;
    if (S_ISDIR(st.st_mode)) {
      isdir_ = true;
      fd_ = -1;
    } else {
      isdir_ = false;
      fd_ = ::open(p.c_str(), O_RDONLY);
      if (fd_ < 0) continue;
    }
    open_ = true;
    gen_ = sd_gen;
    flags_ = O_RDONLY;
    return true;
  }
}

bool FsFile::close() {
  bool ok = true;
  if (open_) {
    bool writable = (flags_ & O_ACCMODE) == O_WRONLY;
    if (writable) {
      ok = sd_alive() && gen_ == sd_gen;
      if (ok) sd_bus(512);
    }
    if (fd_ >= 0) ::close(fd_);
    if (isdir_ && dirp_) closedir((DIR *)dirp_);
  }
  open_ = false;
  isdir_ = false;
  fd_ = -1;
  dirp_ = nullptr;
  return ok;
}

int FsFile::read(void *buf, size_t n) {
  if (!open_ || isdir_ || !sd_alive() || gen_ != sd_gen) return -1;
  sd_bus((uint32_t)n);
  return (int)::read(fd_, buf, n);
}
size_t FsFile::write(const void *buf, size_t n) {
  if (!open_ || isdir_ || !sd_alive() || gen_ != sd_gen) return 0;
  sd_bus((uint32_t)n);
  vt_us += 1500;  // 書き込みは読み込みより遅い
  ssize_t w = ::write(fd_, buf, n);
  return w < 0 ? 0 : (size_t)w;
}
bool FsFile::seekSet(uint64_t pos) {
  if (!open_ || !sd_alive() || gen_ != sd_gen) return false;
  return ::lseek(fd_, (off_t)pos, SEEK_SET) >= 0;
}
uint64_t FsFile::fileSize() {
  if (!open_ || fd_ < 0) return 0;
  struct stat st;
  if (fstat(fd_, &st) != 0) return 0;
  return (uint64_t)st.st_size;
}
size_t FsFile::getName(char *name, size_t size) {
  std::string n = name_;
  size_t p = n.rfind('/');
  if (p != std::string::npos) n = n.substr(p + 1);
  snprintf(name, size, "%s", n.c_str());
  return strlen(name);
}

// ---------------------------------------------------------------------
// 20x4 キャラクタ LCD (HD44780 + PCF8574)
// ---------------------------------------------------------------------
TwoWire Wire;
static bool lcd_hw_present = true;   // --no-lcd で LCD をつながないことにする
static uint8_t i2c_addr = 0;
static uint32_t lcd_absent_writes = 0;  // LCD が無いのに LCD に送ろうとした回数
void TwoWire::begin() {}
void TwoWire::beginTransmission(uint8_t addr) { i2c_addr = addr; }
uint8_t TwoWire::endTransmission(bool) {
  vt_us += 120;
  return (lcd_hw_present && i2c_addr == LCD_I2C_ADDR) ? 0 : 2;  // 2 = アドレスに NACK
}
size_t TwoWire::write(uint8_t) { return 1; }
static uint8_t lcd_ddram[128];
static uint8_t lcd_cgram[64];
static uint8_t lcd_ac = 0;
static bool lcd_cg_mode = false;
static bool lcd_in_create = false;
static bool lcd_backlight = false;
static bool lcd_inited = false;
static uint32_t lcd_writes = 0;
static uint32_t lcd_cgram_stray = 0;  // createChar 以外で CGRAM に書いた (setCursor 忘れ)
static uint32_t lcd_before_init = 0;
static const uint8_t lcd_row_off[4] = { 0x00, 0x40, 0x14, 0x54 };

static void lcd_bus(uint32_t bytes) {
  vt_us += (uint64_t)bytes * 1300;  // I2C 100kHz、1 バイト = 4 ビット x 2 回 x 3 回の書き込み
  lcd_writes += bytes;
}
void LiquidCrystal_I2C::init() {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return;
  }
  Wire.begin();
  lcd_inited = true;
  memset(lcd_ddram, ' ', sizeof(lcd_ddram));
  lcd_ac = 0;
  lcd_cg_mode = false;
  vt_us += 50000;
}
void LiquidCrystal_I2C::begin(uint8_t, uint8_t, uint8_t) { init(); }
void LiquidCrystal_I2C::clear() {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return;
  }
  memset(lcd_ddram, ' ', sizeof(lcd_ddram));
  lcd_ac = 0;
  lcd_cg_mode = false;
  lcd_bus(1);
  vt_us += 2000;
}
void LiquidCrystal_I2C::home() {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return;
  }
  lcd_ac = 0;
  lcd_cg_mode = false;
  lcd_bus(1);
  vt_us += 2000;
}
void LiquidCrystal_I2C::backlight() {
  if (!lcd_hw_present) lcd_absent_writes++;
  lcd_backlight = true;
}
void LiquidCrystal_I2C::noBacklight() { lcd_backlight = false; }
void LiquidCrystal_I2C::setCursor(uint8_t col, uint8_t row) {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return;
  }
  if (row >= 4) row = 3;
  lcd_ac = (uint8_t)((col + lcd_row_off[row]) & 0x7F);
  lcd_cg_mode = false;
  lcd_bus(1);
}
void LiquidCrystal_I2C::createChar(uint8_t location, uint8_t charmap[]) {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return;
  }
  location &= 7;
  lcd_cg_mode = true;
  lcd_ac = (uint8_t)(location << 3);
  lcd_bus(1);
  lcd_in_create = true;
  for (int i = 0; i < 8; i++) write(charmap[i]);
  lcd_in_create = false;
}
size_t LiquidCrystal_I2C::write(uint8_t c) {
  if (!lcd_hw_present) {
    lcd_absent_writes++;
    return 1;
  }
  if (!lcd_inited) lcd_before_init++;
  lcd_bus(1);
  if (lcd_cg_mode) {
    if (!lcd_in_create) lcd_cgram_stray++;
    lcd_cgram[lcd_ac & 0x3F] = c & 0x1F;
    lcd_ac = (uint8_t)((lcd_ac + 1) & 0x3F);
    return 1;
  }
  lcd_ddram[lcd_ac & 0x7F] = c;
  lcd_ac++;
  if (lcd_ac == 0x28) lcd_ac = 0x40;  // 2 行モードの DDRAM は 0x00-0x27 と 0x40-0x67
  else if (lcd_ac >= 0x68) lcd_ac = 0x00;
  return 1;
}

// 4 行を文字列にする。自作文字 (0x00-0x0F) は {n} (n = CGRAM 番号) で表す
static std::string lcd_text_row(int r) {
  std::string out;
  for (int c = 0; c < 20; c++) {
    uint8_t ch = lcd_ddram[(lcd_row_off[r] + c) & 0x7F];
    if (ch < 0x10) {
      out += "{";
      out += (char)('0' + (ch & 7));
      out += "}";
    } else if (ch >= 0x20 && ch < 0x7F) {
      out += (char)ch;
    } else {
      out += "?";
    }
  }
  return out;
}

// 画面の見た目を PPM にする (青地に白文字の一般的な 20x4 LCD 風)
static bool lcd_snap(const char *path) {
  const int DOT = 3, PITCH = 4, CW = 5 * PITCH + 4, RH = 8 * PITCH + 6, M = 22;
  const int W = M * 2 + 20 * CW - 4, H = M * 2 + 4 * RH - 6;
  std::vector<uint8_t> img((size_t)W * H * 3);
  auto put = [&](int x, int y, const uint8_t *rgb) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    memcpy(&img[((size_t)y * W + x) * 3], rgb, 3);
  };
  const uint8_t bezel[3] = { 18, 22, 28 };
  const uint8_t bg_on[3] = { 24, 70, 196 }, bg_off[3] = { 20, 30, 60 };
  const uint8_t dot_on[3] = { 236, 242, 255 };
  const uint8_t dot_off_on[3] = { 34, 84, 212 }, dot_off_off[3] = { 26, 36, 70 };
  const uint8_t *bg = lcd_backlight ? bg_on : bg_off;
  const uint8_t *doff = lcd_backlight ? dot_off_on : dot_off_off;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) put(x, y, (x < 8 || y < 8 || x >= W - 8 || y >= H - 8) ? bezel : bg);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 20; c++) {
      uint8_t ch = lcd_ddram[(lcd_row_off[r] + c) & 0x7F];
      uint8_t rows[8] = { 0 };
      if (ch < 0x10) {
        for (int i = 0; i < 8; i++) rows[i] = lcd_cgram[(ch & 7) * 8 + i];
      } else if (ch >= 0x20 && ch < 0x7F) {
        for (int col = 0; col < 5; col++)
          for (int i = 0; i < 7; i++)
            if (font5x7[ch - 0x20][col] & (1 << i)) rows[i] |= (uint8_t)(0x10 >> col);
      } else if (ch == 0xFF) {
        for (int i = 0; i < 8; i++) rows[i] = 0x1F;
      }
      int x0 = M + c * CW, y0 = M + r * RH;
      for (int i = 0; i < 8; i++)
        for (int col = 0; col < 5; col++) {
          bool on = rows[i] & (0x10 >> col);
          for (int dy = 0; dy < DOT; dy++)
            for (int dx = 0; dx < DOT; dx++) put(x0 + col * PITCH + dx, y0 + i * PITCH + dy, on ? dot_on : doff);
        }
    }
  }
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(img.data(), 1, img.size(), f);
  fclose(f);
  return true;
}

// ---------------------------------------------------------------------
// 制御チャンネル
// ---------------------------------------------------------------------
static void reply(const std::string &s) {
  if (g_quiet) return;
  std::string l = s + "\n";
  ssize_t r = ::write(4, l.data(), l.size());
  (void)r;
}

static std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> v;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') i++;
    size_t j = i;
    while (j < s.size() && s[j] != ' ') j++;
    if (j > i) v.push_back(s.substr(i, j - i));
    i = j;
  }
  return v;
}

static bool load_file(const std::string &path, uint8_t *dst, size_t n) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  size_t r = fread(dst, 1, n, f);
  fclose(f);
  return r > 0;
}
static bool save_file(const std::string &path, const uint8_t *src, size_t n) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(src, 1, n, f);
  fclose(f);
  return true;
}

static uint32_t pending_advance_ms = 0;

static void ctl_exec(const std::string &line) {
  if (line.compare(0, 8, "trigger ") == 0) {
    size_t p = line.find(" => ");
    if (p == std::string::npos) { reply("err trigger syntax"); return; }
    Trigger t;
    t.prefix = line.substr(8, p - 8);
    t.cmd = line.substr(p + 4);
    t.thresh = -1;
    if (t.prefix.compare(0, 3, "P>=") == 0) t.thresh = atol(t.prefix.c_str() + 3);
    triggers.push_back(t);
    reply("ok");
    return;
  }
  std::vector<std::string> a = split(line);
  if (a.empty()) return;
  const std::string &c = a[0];
  if (c == "sd" && a.size() >= 2) {
    if (a[1] == "remove") { sd_present = false; reply("ok"); }
    else if (a[1] == "insert") { if (!sd_present) sd_gen++; sd_present = true; reply("ok"); }
    else if (a[1] == "swap") { sd_gen++; sd_present = true; reply("ok"); }
    else if (a[1] == "root" && a.size() >= 3) { sd_root = a[2]; reply("ok"); }
    else reply("err sd");
  } else if (c == "usb" && a.size() >= 2) {
    usb_connected = a[1] != "0";
    reply("ok");
  } else if (c == "snap" && a.size() >= 2) {
    reply(lcd_snap(a[1].c_str()) ? "ok" : "err snap");
  } else if (c == "lcd") {
    std::string t = lcd_text_row(0) + "|" + lcd_text_row(1) + "|" + lcd_text_row(2) + "|" + lcd_text_row(3);
    if (a.size() >= 2) {  // lcd <path> : ファイルに書く (トリガーから使う)
      FILE *f = fopen(a[1].c_str(), "w");
      if (f) {
        fputs(t.c_str(), f);
        fclose(f);
      }
    }
    reply("lcd " + t);
  } else if (c == "card" && a.size() >= 3) {
    if (a[1] == "load") reply(load_file(a[2], card_mem, sizeof(card_mem)) ? "ok" : "err load");
    else if (a[1] == "dump") reply(save_file(a[2], card_mem, sizeof(card_mem)) ? "ok" : "err dump");
    else if (a[1] == "fill") { memset(card_mem, (int)strtol(a[2].c_str(), nullptr, 16), sizeof(card_mem)); reply("ok"); }
    else reply("err card");
  } else if (c == "spi" && a.size() >= 3) {
    if (a[1] == "load") reply(load_file(a[2], sram23.mem, 0x8000) ? "ok" : "err load");
    else if (a[1] == "dump") reply(save_file(a[2], sram23.mem, 0x8000) ? "ok" : "err dump");
    else if (a[1] == "fill") { memset(sram23.mem, (int)strtol(a[2].c_str(), nullptr, 16), sizeof(sram23.mem)); reply("ok"); }
    else reply("err spi");
  } else if (c == "ram62" && a.size() >= 3) {
    if (a[1] == "dump") reply(save_file(a[2], ram62, sizeof(ram62)) ? "ok" : "err dump");
    else if (a[1] == "load") reply(load_file(a[2], ram62, sizeof(ram62)) ? "ok" : "err load");
    else if (a[1] == "fill") { memset(ram62, (int)strtol(a[2].c_str(), nullptr, 16), sizeof(ram62)); reply("ok"); }
    else reply("err ram62");
  } else if (c == "stuck" && a.size() >= 2) {
    // stuck addr|data <ビット> <0|1> / stuck clear : バス線の H / L 固定
    if (a[1] == "clear") {
      addr_stuck_mask = addr_stuck_val = 0;
      data_stuck_mask = data_stuck_val = 0;
      reply("ok");
    } else if (a.size() >= 4) {
      int bit = atoi(a[2].c_str()), v = atoi(a[3].c_str());
      if (a[1] == "addr") {
        addr_stuck_mask |= (uint16_t)(1u << bit);
        if (v) addr_stuck_val |= (uint16_t)(1u << bit);
        else addr_stuck_val &= (uint16_t)~(1u << bit);
      } else {
        data_stuck_mask |= (uint8_t)(1u << bit);
        if (v) data_stuck_val |= (uint8_t)(1u << bit);
        else data_stuck_val &= (uint8_t)~(1u << bit);
      }
      reply("ok");
    } else reply("err stuck");
  } else if (c == "glitch" && a.size() >= 2) {
    glitch_addr = (int)strtol(a[1].c_str(), nullptr, 16);
    reply("ok");
  } else if (c == "cardpresent" && a.size() >= 2) {
    card_present = a[1] != "0";
    reply("ok");
  } else if (c == "poke" && a.size() >= 4) {
    // poke card|spi ADDR VALUE (16 進)
    unsigned ad = (unsigned)strtoul(a[2].c_str(), nullptr, 16);
    unsigned v = (unsigned)strtoul(a[3].c_str(), nullptr, 16);
    if (a[1] == "card" && ad < 0x8000) card_mem[ad] = (uint8_t)v;
    else if (a[1] == "spi" && ad < 0x10000) sram23.mem[ad] = (uint8_t)v;
    reply("ok");
  } else if (c == "advance" && a.size() >= 2) {
    pending_advance_ms += (uint32_t)atol(a[1].c_str());  // 実行は次のループで。完了時に "advanced" を返す
  } else if (c == "inject" && a.size() >= 2) {
    // inject <16進> : PC からシリアルに届いたバイトとして渡す (例: inject 21 = '!')
    for (size_t i = 0; i + 1 < a[1].size(); i += 2) rx_buf.push_back((char)strtol(a[1].substr(i, 2).c_str(), nullptr, 16));
    reply("ok");
  } else if (c == "bus") {
    // アドレス・データの向き (1 = 入力) と制御線の出力値
    char b[120];
    snprintf(b, sizeof(b), "bus addr_dir=%02X%02X data_dir=%02X ctrl=%02X ctrl_dir=%02X", mcp[6].r[1][R_IODIR], mcp[6].r[0][R_IODIR], mcp[7].r[0][R_IODIR],
             mcp[7].r[1][R_OLAT], mcp[7].r[1][R_IODIR]);
    reply(b);
  } else if (c == "now") {
    reply("now " + std::to_string(millis()));
  } else if (c == "diag") {
    if (a.size() >= 2 && a[1] == "reset") {
      diag = Diag();
      lcd_cgram_stray = lcd_before_init = lcd_absent_writes = 0;
      reply("ok");
      return;
    }
    char b[480];
    snprintf(b, sizeof(b),
             "diag no_txn=%u spi_dead=%u over_clock=%u bus_conflict=%u contention=%u addr_change_in_write=%u data_change_in_write=%u oe_we_overlap=%u mcp_unaddressed=%u sd_stale=%u lcd_cgram_stray=%u lcd_before_init=%u lcd_absent_writes=%u sd_ops=%u lcd_writes=%u now=%lu",
             diag.no_txn, diag.spi_dead, diag.over_clock, diag.bus_conflict, diag.contention, diag.addr_change_in_write, diag.data_change_in_write,
             diag.oe_we_overlap, diag.mcp_unaddressed, diag.sd_stale, lcd_cgram_stray, lcd_before_init, lcd_absent_writes, sd_ops, lcd_writes, millis());
    reply(b);
  } else if (c == "state") {
    char b[200];
    snprintf(b, sizeof(b), "state sd_present=%d sd_mounted=%d usb=%d spi_init=%d", sd_present ? 1 : 0, sd_alive() ? 1 : 0, usb_connected ? 1 : 0, spi_init ? 1 : 0);
    reply(b);
  } else if (c == "quit") {
    g_quit = true;
    reply("ok");
  } else {
    reply("err unknown " + c);
  }
}

static void sim_poll_io() {
  static uint64_t last_us = (uint64_t)-1;
  if (last_us != (uint64_t)-1 && vt_us - last_us < 100) return;
  last_us = vt_us;
  tx_flush();
  struct pollfd pf[2] = { { 0, POLLIN, 0 }, { 3, POLLIN, 0 } };
  if (poll(pf, 2, 0) <= 0) return;
  char b[4096];
  if (pf[0].revents & POLLIN) {
    ssize_t n = ::read(0, b, sizeof(b));
    if (n > 0) rx_buf.append(b, (size_t)n);
    else if (n == 0) g_quit = true;
  }
  if (pf[1].revents & POLLIN) {
    ssize_t n = ::read(3, b, sizeof(b));
    if (n > 0) ctl_buf.append(b, (size_t)n);
    size_t p;
    while ((p = ctl_buf.find('\n')) != std::string::npos) {
      std::string l = ctl_buf.substr(0, p);
      ctl_buf.erase(0, p + 1);
      ctl_exec(l);
    }
  }
}

// ---------------------------------------------------------------------
// main
// ---------------------------------------------------------------------
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--sd-root" && i + 1 < argc) sd_root = argv[++i];
    else if (a == "--no-sd") sd_present = false;
    else if (a == "--no-usb") usb_connected = false;
    else if (a == "--realtime") realtime = true;
    else if (a == "--no-lcd") lcd_hw_present = false;
    else if (a == "--no-spi-sram") spi_sram_present = false;
    else if (a == "--no-card") card_present = false;
    else {
      fprintf(stderr, "unknown option %s\n", a.c_str());
      return 2;
    }
  }
  for (int i = 0; i < 64; i++) pin_level[i] = 1;
  for (int i = 0; i < 8; i++) mcp[i].reset();
  memset(card_mem, 0xA5, sizeof(card_mem));
  memset(ram62, 0x5A, sizeof(ram62));
  memset(sram23.mem, 0x3C, sizeof(sram23.mem));
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
  fcntl(3, F_SETFL, fcntl(3, F_GETFL) | O_NONBLOCK);
  // CS ピンの変化を追うため、MCP の CS は Arduino の D4 (初期 HIGH)
  setup();
  uint32_t idle = 0;
  while (!g_quit) {
    sim_poll_io();
    loop();
    if (pending_advance_ms) {
      uint32_t n = pending_advance_ms;
      pending_advance_ms = 0;
      for (uint32_t i = 0; i < n && !g_quit; i++) {
        vt_us += 1000;
        sim_poll_io();
        loop();
      }
      reply("advanced");
      continue;
    }
    vt_us += 1000;  // 待機中は 1ms ずつ進む
    if (realtime) usleep(1000);
    else if (++idle % 32 == 0) usleep(200);
  }
  tx_flush();
  return 0;
}
