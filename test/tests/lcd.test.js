// 20x4 キャラクタ LCD の表示テスト (LCD モデルの文字を直接確かめる。画像は test/out/lcd に保存)
const test = require('node:test');
const { afterEach } = test;
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { Firmware } = require('../lib/fw');
const { ppmToPng } = require('../lib/png');

const SHOTS = path.join(__dirname, '..', 'out', 'lcd');
fs.mkdirSync(SHOTS, { recursive: true });
afterEach(() => Firmware.closeAll());

const pattern = (seed) => {
  const b = Buffer.alloc(0x8000);
  for (let i = 0; i < b.length; i++) b[i] = (i * 31 + (i >> 8) * 7 + seed) & 0xff;
  return b;
};
// 待機中は実時間で進める (画像の変換などで間が空いても、本体の 8 秒タイムアウトが働かないように)
async function boot(opts = {}) {
  const fw = new Firmware({ ...opts, args: ['--realtime', ...(opts.args || [])] });
  await fw.advance(50);
  return fw;
}
// LCD の 4 行 (自作文字は {n})
async function rows(fw, name) {
  if (name) ppmToPng(await fw.snap(name), path.join(SHOTS, name + '.png'));
  const r = await fw.ctl('lcd');
  return r.slice(4).split('|');
}
const parse = (text) => text.split('|');
// 自作文字を 1 文字として数えた長さ
const width = (row) => row.replace(/\{\d\}/g, '#').length;
// 進捗バー 15 文字を「塗った列数」(0-75) に直す
function barUnits(row) {
  const cells = row.match(/\{\d\}/g).slice(0, 15);
  let u = 0;
  for (const c of cells) {
    const n = Number(c[1]);
    if (n <= 4) u += n + 1; // {0}..{4} = 1..5 列
  }
  return u;
}
async function expectCleanLcd(fw) {
  const d = await fw.diag();
  assert.equal(d.lcd_cgram_stray, 0, 'createChar の後に setCursor せず書いた');
  assert.equal(d.lcd_before_init, 0, '初期化前に書いた');
  for (const k of ['no_txn', 'spi_dead', 'over_clock', 'bus_conflict', 'lcd_absent_writes']) assert.equal(d[k], 0, k);
}

test('待機画面: 名前・SD/PC の状態・直前の結果', async () => {
  const fw = await boot();
  const r = await rows(fw, 'idle');
  assert.deepEqual(r, ['FUJISOKU SRAM   v2.5', 'SD:OK      PC:OK    ', 'Ready               ', 'Last:---            ']);
  await expectCleanLcd(fw);
});

test('SD の抜き差しと USB の接続が待機画面にすぐ出る', async () => {
  const fw = await boot();
  await fw.ctl('sd remove');
  await fw.advance(2000);
  assert.equal((await rows(fw, 'idle_nosd'))[1], 'SD:NONE    PC:OK    ');
  await fw.ctl('sd insert');
  await fw.cmd('sdcheck');
  await fw.advance(16000);
  assert.equal((await rows(fw))[1], 'SD:OK      PC:OK    ');
  await fw.ctl('usb 0');
  await fw.advance(300);
  const r = await rows(fw, 'idle_nousb');
  assert.equal(r[1], 'SD:OK      PC:--    ');
  assert.equal(r[2], 'Waiting for PC (USB)');
  await expectCleanLcd(fw);
});

test('処理中: 処理名・経過時間・段階・進捗バー・アドレスと速度', async () => {
  const fw = await boot();
  await fw.cardLoad(pattern(1));
  const txt = path.join(fw.tmp, 'mid.txt');
  await fw.ctl(`trigger P>=16384 => lcd ${txt}`);
  await fw.ctl(`trigger P>=16640 => snap ${path.join(fw.tmp, 'mid.ppm')}`);
  await fw.cmd('B');
  ppmToPng(path.join(fw.tmp, 'mid.ppm'), path.join(SHOTS, 'progress.png'));
  const r = parse(fs.readFileSync(txt, 'utf8'));
  assert.match(r[0], /^BACKUP {9}\d\d:\d\d$/);
  assert.equal(r[1], 'Reading card        ');
  // P 行は LCD 更新の直前に出るので、LCD はひとつ前の値 (49%) のこともある
  assert.match(r[2], / {2}(49|50)%$/, r[2]);
  const u = barUnits(r[2]);
  assert.ok(u >= 36 && u <= 38, 'バーは半分: ' + u);
  // アドレスと速度の行は 0.5 秒ごとの更新なので少し遅れる
  const m = r[3].match(/^0x([0-9A-F]{4})\/0x8000 +[\d.]+k\/s$/);
  assert.ok(m, r[3]);
  assert.ok(parseInt(m[1], 16) >= 0x3000 && parseInt(m[1], 16) <= 0x4100, r[3]);
  for (const row of r) assert.equal(width(row), 20);
});

test('進捗バーは 75 段階で正確に伸びる', async () => {
  const fw = await boot();
  await fw.cmd('ui begin 75 Bar_test');
  for (let k = 0; k <= 75; k++) {
    await fw.cmd(`ui prog ${k}`);
    const r = await rows(fw);
    assert.equal(barUnits(r[2]), k, `k=${k}: ${r[2]}`);
    assert.ok(r[2].endsWith(String(Math.floor((k * 100) / 75)).padStart(3) + '%'), r[2]);
    assert.equal(width(r[2]), 20);
  }
  await fw.cmd('ui end ok Done');
  await expectCleanLcd(fw);
});

test('結果画面: 完了・不一致・SD なし・SD 抜け・中断・ファイル不正', async () => {
  const fw = await boot();
  await fw.cardLoad(pattern(2));
  await fw.cmd('B');
  let r = await rows(fw, 'result_ok');
  assert.equal(r[0], '{6} BACKUP        DONE');
  assert.deepEqual(r.slice(1, 3), ['32768 bytes ->      ', 'fujisoku.bin        ']);
  assert.match(r[3], /^Time \d\d:\d\d {10}$/);

  await fw.ctl('poke card 100 00');
  await fw.cmd('V');
  r = await rows(fw, 'result_mismatch');
  assert.equal(r[0], '! VERIFY    MISMATCH');
  assert.deepEqual(r.slice(1, 3), ['mismatches=1        ', 'first=0x0100        ']);

  fs.writeFileSync(path.join(fw.sdRoot, 'short.bin'), Buffer.alloc(100));
  await fw.cmd('sd load f short');
  r = await rows(fw, 'result_badsize');
  assert.match(r[0], /^\{7\} RESTORE +BAD FILE$/);
  assert.match(r[3], /\[BADSIZE\]/);

  await fw.cmd('sd verify f nothing');
  assert.match((await rows(fw))[0], /NO FILE$/);

  await fw.ctl('trigger P>=8000 => inject 21');
  await fw.cmd('R');
  r = await rows(fw, 'result_cancel');
  assert.match(r[0], /^- READ CARD +CANCELED$/);
  assert.match(r[1], /^Stopped at 0x/);

  await fw.ctl('trigger P>=8000 => sd remove');
  await fw.cmd('B');
  r = await rows(fw, 'result_sdlost');
  assert.match(r[0], /SD LOST$/);
  assert.deepEqual(r.slice(1, 3), ['SD card was removed.', 'Insert it, retry    ']);

  await fw.cmd('V');
  r = await rows(fw, 'result_nosd');
  assert.match(r[0], /^\{7\} VERIFY +NO SD$/);
  assert.deepEqual(r.slice(1, 3), ['Insert an SD card   ', 'and try again       ']);
  assert.match(r[3], /\[NOSD\]/);
  for (const row of r) assert.equal(width(row), 20);
  await expectCleanLcd(fw);
});

test('結果画面は 15 秒後に待機画面へ戻り、結果は Last: に残る', async () => {
  const fw = await boot();
  await fw.cmd('B');
  await fw.advance(5000);
  assert.match((await rows(fw))[0], /DONE$/);
  await fw.advance(11000);
  const r = await rows(fw, 'idle_last');
  assert.equal(r[0], 'FUJISOKU SRAM   v2.5');
  assert.equal(r[3], 'Last:{6} BACKUP OK    ');
});

test('PC 主導の表示 (ui begin / prog / end)。PC が止まったら PC LOST', async () => {
  const fw = await boot();
  await fw.cmd('ui begin 32768 Card_to_PC');
  await fw.cmd('ui prog 12288 Reading_card');
  await fw.advance(1500);  // アドレス・速度の行は 0.5 秒ごとに更新される
  let r = await rows(fw, 'pc_progress');
  assert.match(r[0], /^Card to PC {5}\d\d:\d\d$/);
  assert.equal(r[1], 'Reading card        ');
  assert.ok(r[2].endsWith('  37%'));
  assert.match(r[3], /^0x3000\/0x8000/);
  await fw.cmd('ui end ok Saved_to_PC');
  r = await rows(fw, 'pc_done');
  assert.match(r[0], /^\{6\} Card to PC +DONE$/);
  assert.equal(r[1], 'Saved to PC         ');

  await fw.cmd('ui begin 100 Write_from_PC');
  const lost = fw.waitLine((l) => l === 'E UI timeout');
  await fw.advance(9000);
  await lost;
  r = await rows(fw, 'pc_lost');
  assert.match(r[0], /PC LOST$/);
  assert.equal(r[1], 'Lost contact with PC');
  await expectCleanLcd(fw);
});

test('長い処理名や文字列でも 20 文字を超えず、ほかの行を壊さない', async () => {
  const fw = await boot();
  await fw.cmd('ui begin 100000 ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789');
  await fw.cmd('ui prog 50000 abcdefghijklmnopqrstuvwxyz0123456789');
  const r = await rows(fw, 'long_text');
  for (const row of r) assert.equal(width(row), 20, row);
  assert.match(r[0], /^ABCDEFGHIJKLMNO\d\d:\d\d$/);
  assert.equal(r[1], 'abcdefghijklmnopqrst');
  await fw.cmd('ui end err ' + 'X'.repeat(60));
  for (const row of await rows(fw)) assert.equal(width(row), 20, row);
  await expectCleanLcd(fw);
});

test('処理中の LCD 書き換えは差分だけ (処理を遅くしない)', async () => {
  const fw = await boot();
  const before = (await fw.diag()).lcd_writes;
  await fw.cmd('B');
  const used = (await fw.diag()).lcd_writes - before;
  // 32KB のバックアップ全体で数百文字程度 (全画面を毎回書くと数千になる)
  assert.ok(used < 700, 'LCD への書き込み ' + used + ' 文字');
});
