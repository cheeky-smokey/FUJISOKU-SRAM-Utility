// ホストでビルドしたファームウェア (sim) を Node から操作する小さなラッパ
//   stdin/stdout = シリアル、fd3 = 制御コマンド、fd4 = 制御の応答
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

class Firmware {
  constructor(opts = {}) {
    this.bin = opts.bin || path.join(__dirname, '..', 'out', 'fsu_sim');
    this.sdRoot = opts.sdRoot || fs.mkdtempSync(path.join(os.tmpdir(), 'fsu_sd_'));
    this.tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fsu_tmp_'));
    const args = ['--sd-root', this.sdRoot, ...(opts.args || [])];
    this.p = spawn(this.bin, args, { stdio: ['pipe', 'pipe', 'inherit', 'pipe', 'pipe'] });
    this.lines = [];
    this.waiters = [];
    this.onLine = null;
    this.rxbuf = '';
    this.ctlq = [];
    this.ctlbuf = '';
    this.exited = new Promise((res) => this.p.on('exit', res));
    Firmware.all.add(this);
    this.p.stdout.on('data', (d) => this._data(d));
    this.p.stdio[4].on('data', (d) => this._ctl(d));
    this.p.stdin.on('error', () => {});
  }
  _data(d) {
    this.rxbuf += d.toString('latin1');
    let i;
    while ((i = this.rxbuf.indexOf('\n')) >= 0) {
      const l = this.rxbuf.slice(0, i).replace(/\r$/, '');
      this.rxbuf = this.rxbuf.slice(i + 1);
      this.lines.push(l);
      if (this.onLine) this.onLine(l);
      this.waiters = this.waiters.filter((w) => !w(l));
    }
  }
  _ctl(d) {
    this.ctlbuf += d.toString();
    let i;
    while ((i = this.ctlbuf.indexOf('\n')) >= 0) {
      const l = this.ctlbuf.slice(0, i);
      this.ctlbuf = this.ctlbuf.slice(i + 1);
      const r = this.ctlq.shift();
      if (r) r(l);
    }
  }
  // 制御コマンドを送って応答を 1 行待つ
  ctl(cmd) {
    return new Promise((res) => {
      this.ctlq.push(res);
      this.p.stdio[3].write(cmd + '\n');
    });
  }
  send(s) {
    this.p.stdin.write(s);
  }
  // 条件に合う行が来るまで待つ (これまでに受け取った行は見ない)
  waitLine(pred, timeoutMs = 20000) {
    return new Promise((res, rej) => {
      const t = setTimeout(() => rej(new Error('timeout waiting for line')), timeoutMs);
      this.waiters.push((l) => {
        if (pred(l)) {
          clearTimeout(t);
          res(l);
          return true;
        }
        return false;
      });
    });
  }
  // 1 コマンド送って、終了行 (OK/NG/ERR で始まる行) までの全行を返す
  async cmd(line, timeoutMs = 60000) {
    const start = this.lines.length;
    const done = this.waitLine((l) => /^(OK|NG|ERR|HELLO|STATUS|LCD) /.test(l), timeoutMs);
    this.send(line + '\n');
    const last = await done;
    return { last, lines: this.lines.slice(start) };
  }
  async diag() {
    const r = await this.ctl('diag');
    const o = {};
    for (const kv of r.split(' ').slice(1)) {
      const [k, v] = kv.split('=');
      o[k] = Number(v);
    }
    return o;
  }
  // FUJISOKU カード / SPI SRAM の中身を読み書きする (32KB)
  async cardDump() {
    const f = path.join(this.tmp, 'card.bin');
    await this.ctl('card dump ' + f);
    return fs.readFileSync(f);
  }
  async cardLoad(buf) {
    const f = path.join(this.tmp, 'card_in.bin');
    fs.writeFileSync(f, buf);
    return this.ctl('card load ' + f);
  }
  async spiDump() {
    const f = path.join(this.tmp, 'spi.bin');
    await this.ctl('spi dump ' + f);
    return fs.readFileSync(f);
  }
  async spiLoad(buf) {
    const f = path.join(this.tmp, 'spi_in.bin');
    fs.writeFileSync(f, buf);
    return this.ctl('spi load ' + f);
  }
  async snap(name) {
    const ppm = path.join(this.tmp, name + '.ppm');
    await this.ctl('snap ' + ppm);
    return ppm;
  }
  async advance(ms) {
    const w = new Promise((res) => this.ctlq.push(res));
    this.p.stdio[3].write('advance ' + ms + '\n');
    return w;
  }
  async close() {
    try {
      await this.ctl('quit');
    } catch (e) {}
    this.p.stdin.end();
    await Promise.race([this.exited, new Promise((r) => setTimeout(r, 2000))]);
    this.p.kill();
    Firmware.all.delete(this);
  }
}
Firmware.all = new Set();
// テストが途中で失敗しても子プロセスが残らないようにする
Firmware.closeAll = async () => {
  for (const fw of [...Firmware.all]) {
    try {
      fw.p.kill('SIGKILL');
    } catch (e) {}
    Firmware.all.delete(fw);
  }
};
module.exports = { Firmware };
