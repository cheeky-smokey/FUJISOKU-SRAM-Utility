// ファームウェア (ホストシミュレータ上) の動作テスト
//   実行: test/build.sh && test/build.sh fsu_sim_article -DCTRL_MAP_ARTICLE=1 && node --test test/tests
const test = require('node:test');
const { afterEach } = test;
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { Firmware } = require('../lib/fw');

const OUT = path.join(__dirname, '..', 'out');
afterEach(() => Firmware.closeAll());

function pattern(seed = 0) {
  const b = Buffer.alloc(0x8000);
  for (let i = 0; i < b.length; i++) b[i] = (i * 31 + (i >> 8) * 7 + seed) & 0xff;
  return b;
}
async function boot(opts = {}) {
  const fw = new Firmware(opts);
  await fw.advance(50);
  return fw;
}
async function expectClean(fw, extra = {}) {
  const d = await fw.diag();
  const want = { no_txn: 0, spi_dead: 0, over_clock: 0, bus_conflict: 0, contention: 0, addr_change_in_write: 0, data_change_in_write: 0, oe_we_overlap: 0, mcp_unaddressed: 0, lcd_cgram_stray: 0, lcd_before_init: 0, lcd_absent_writes: 0, ...extra };
  for (const k of Object.keys(want)) assert.equal(d[k], want[k], `diag ${k}`);
}
const sdfile = (fw, n = 'fujisoku.bin') => path.join(fw.sdRoot, n);

test('起動: SD があれば READY、バスの不正利用なし', async () => {
  const fw = await boot();
  assert.ok(fw.lines.includes('E SD 1'));
  assert.ok(fw.lines.some((l) => l.startsWith('FUJISOKU SRAM Utility 2.8 ready (LCD 20x4, SD on, 62256 off, SPI SRAM ok, card in)')));
  const r = await fw.cmd('hello');
  assert.match(r.last, /^HELLO FSU 2\.8 size=32768 cardmax=262144 chunk=64 lcd=20x4 sram62256=0 sdcard=1 spisram=ok carddet=1 battdet=1 battlow=1500 battnone=500$/);
  await expectClean(fw);
  await fw.close();
});

test('wr / rd / fill でカードを読み書きできる', async () => {
  const fw = await boot();
  await fw.cardLoad(Buffer.alloc(0x8000, 0xff));
  let r = await fw.cmd('wr f 0x0100 DEADBEEF');
  assert.equal(r.last, 'OK wr 256 4');
  r = await fw.cmd('rd f 0x100 4');
  assert.deepEqual(r.lines.slice(0, 2), ['D 0100 DEADBEEF', 'OK rd 4']);
  r = await fw.cmd('fill f 0x7ff0 16 0x5a');
  assert.equal(r.last, 'OK fill 16');
  const card = await fw.cardDump();
  assert.equal(card.subarray(0x100, 0x104).toString('hex'), 'deadbeef');
  assert.equal(card.subarray(0x7ff0, 0x8000).toString('hex'), '5a'.repeat(16));
  assert.equal(card[0xff], 0xff);
  assert.equal(card[0x104], 0xff);
  // 範囲外・不正な引数
  assert.match((await fw.cmd('rd f 0x3ffff 2')).last, /^ERR RANGE/);
  assert.match((await fw.cmd('wr f 0 ZZ')).last, /^ERR ARG/);
  assert.match((await fw.cmd('wr f 0 ABC')).last, /^ERR ARG/);
  assert.match((await fw.cmd('bogus')).last, /^ERR CMD/);
  await expectClean(fw);
  await fw.close();
});

test('rd は 64 バイトごとの行で全域を読める', async () => {
  const fw = await boot();
  const pat = pattern(3);
  await fw.cardLoad(pat);
  const r = await fw.cmd('rd f 0 32768');
  assert.equal(r.last, 'OK rd 32768');
  const lines = r.lines.filter((l) => l.startsWith('D '));
  assert.equal(lines.length, 512);
  const got = Buffer.concat(lines.map((l) => Buffer.from(l.split(' ')[2], 'hex')));
  assert.ok(got.equals(pat));
  await fw.close();
});

{
  test('SPI SRAM: R / C / X / E', async () => {
    const fw = await boot();
    const pat = pattern(5);
    await fw.cardLoad(pat);
    let r = await fw.cmd('R');
    assert.match(r.last, /^OK copy 32768 bytes/);
    assert.ok((await fw.spiDump()).equals(pat));
    assert.match((await fw.cmd('X')).last, /^OK cmp all 32768 bytes match/);
    await fw.cardLoad(pattern(9));
    r = await fw.cmd('X');
    assert.match(r.last, /^NG cmp/);
    r = await fw.cmd('C');
    assert.match(r.last, /^OK copy/);
    assert.ok((await fw.cardDump()).equals(pat));
    r = await fw.cmd('E');
    assert.match(r.last, /^OK fill/);
    assert.ok((await fw.spiDump()).every((b) => b === 0xff));
    await expectClean(fw);
    await fw.close();
  });
}

test('SPI SRAM 自己診断: 合格のときは中身を元に戻し、SPI SRAM が無ければ NG', async () => {
  let fw = await boot();
  await fw.ctl('spi fill 3c');
  let r = await fw.cmd('T');
  assert.equal(r.last, 'OK test SPI SRAM OK');
  assert.ok(r.lines.includes('T mode=40'), r.lines.join('\n'));
  assert.ok(r.lines.includes('T readback=A0A1A2A3A4A5A6A7A8A9AAABACADAEAF bad=0'));
  // 試験で書いた両端も含めて、中身は元のまま
  assert.ok((await fw.spiDump()).every((b) => b === 0x3c));
  assert.match((await fw.cmd('status')).last, / spi=40 /);
  await expectClean(fw);
  await fw.close();

  fw = await boot({ args: ['--no-spi-sram'] });
  r = await fw.cmd('test s');
  assert.equal(r.last, 'NG test no response (check /CS D6) bad=64');
  assert.ok(r.lines.includes('T mode=FF'));
  assert.match((await fw.cmd('status')).last, / spi=FF /);
  await fw.close();
});

test('rd / wr / fill: 対象ごとの範囲 (カード 256KB、SPI SRAM 64KB) と、桁あふれを範囲外として断る', async () => {
  const fw = await boot();
  for (const c of ['rd f 0xFFFFFFF0 0x20', 'rd s 0xFFFF 2', 'wr f 0xFFFFFFFF 0011', 'fill f 0xFFFFFF00 0x200 0', 'rd f 0x40000 1', 'rd f 0x3FFFF 2', 'wr s 0x10000 00']) {
    assert.equal((await fw.cmd(c)).last, 'ERR RANGE out of range', c);
  }
  assert.equal((await fw.cmd('rd f 0x3FFFF 1')).last, 'OK rd 1');
  assert.equal((await fw.cmd('rd s 0xFFFF 1')).last, 'OK rd 1');
  await fw.close();
});

test('カードの 32KB より上 (A15-A17): 256KB のカードでは別の場所、32KB のカードでは先頭と重なる', async () => {
  // 256KB のカード: A15 / A16 / A17 がそれぞれ別の場所を指す
  let fw = await boot({ args: ['--card-kb', '256'] });
  await fw.ctl('card fill ff');
  const spots = { 0x00000: '01', 0x08000: '02', 0x10000: '03', 0x20000: '04', 0x3ffc0: '05' };
  for (const [a, v] of Object.entries(spots)) assert.match((await fw.cmd(`wr f ${a} ${v}`)).last, /^OK wr /);
  for (const [a, v] of Object.entries(spots)) {
    const r = await fw.cmd(`rd f ${a} 1`);
    const want = 'D ' + Number(a).toString(16).toUpperCase().padStart(4, '0') + ' ' + v;
    assert.ok(r.lines.includes(want), `${want} / ${r.lines.join('|')}`);
  }
  const dump = await fw.cardDump();
  assert.equal(dump.length, 0x40000);
  for (const [a, v] of Object.entries(spots)) assert.equal(dump[Number(a)], parseInt(v, 16));
  await expectClean(fw);
  await fw.close();

  // 32KB のカード: 上位のアドレス線がつながっていないので、0x8000 に書くと 0x0000 が変わる
  fw = await boot();
  await fw.ctl('card fill ff');
  await fw.cmd('wr f 0x8000 5A');
  assert.ok((await fw.cmd('rd f 0 1')).lines.includes('D 0000 5A'));
  await fw.close();
});

test('B (バックアップ) → V (照合) → L (書き込み) の往復', async () => {
  const fw = await boot();
  const pat = pattern(1);
  await fw.cardLoad(pat);
  let r = await fw.cmd('B');
  assert.match(r.last, /^OK sd /);
  assert.ok(fs.readFileSync(sdfile(fw)).equals(pat));
  assert.match((await fw.cmd('V')).last, /^OK sd all 32768 bytes match/);
  // カードを 3 か所書き換える → 不一致
  await fw.ctl('poke card 0 00');
  await fw.ctl('poke card 1234 ee');
  await fw.ctl('poke card 7fff 11');
  r = await fw.cmd('V');
  assert.match(r.last, /^NG sd/);
  assert.match(r.last, /3/);
  // 書き戻す
  r = await fw.cmd('L');
  assert.match(r.last, /^OK sd/);
  assert.ok((await fw.cardDump()).equals(pat));
  assert.match((await fw.cmd('V')).last, /^OK sd all 32768 bytes match/);
  await expectClean(fw);
  await fw.close();
});

test('sd コマンド: 名前付き保存・一覧・書き込み後の照合・サイズ不正', async () => {
  const fw = await boot();
  const pat = pattern(2);
  await fw.cardLoad(pat);
  assert.match((await fw.cmd('sd save f game1')).last, /^OK sd/);
  assert.ok(fs.existsSync(sdfile(fw, 'game1.bin')));
  const ls = await fw.cmd('sd ls');
  assert.ok(ls.lines.some((l) => /^F game1\.bin 32768$/.test(l)), ls.lines.join('|'));
  await fw.cardLoad(Buffer.alloc(0x8000, 0));
  const r = await fw.cmd('sd load f game1 v');
  assert.match(r.last, /^OK sd/);
  assert.ok((await fw.cardDump()).equals(pat));
  // サイズが違うファイルは書き込まない
  fs.writeFileSync(sdfile(fw, 'bad.bin'), Buffer.alloc(1000, 1));
  await fw.cardLoad(pat);
  const bad = await fw.cmd('sd load f bad');
  assert.match(bad.last, /^ERR BADSIZE/);
  assert.ok((await fw.cardDump()).equals(pat), 'サイズ不正のとき中身は変わらない');
  assert.match((await fw.cmd('sd verify f bad')).last, /^NG .*BADSIZE|^NG/);
  assert.match((await fw.cmd('sd load f nothing')).last, /^ERR NOFILE/);
  assert.match((await fw.cmd('sd load f bad/name')).last, /^ERR ARG/);
  await fw.close();
});

test('SD なしで起動しても止まらない。後から挿して使える', async () => {
  const fw = await boot({ args: ['--no-sd'] });
  assert.ok(fw.lines.some((l) => l.includes('ready')), '起動完了');
  assert.ok(!fw.lines.some((l) => l.startsWith('ERR')), 'エラーは出ない');
  assert.ok(fw.lines.includes('E SD 0'));
  const st = await fw.cmd('status');
  assert.match(st.last, /^STATUS sd=0 busy=0/);
  // カードなしで SD の処理 → 分かりやすいエラー (固まらない)
  let r = await fw.cmd('B');
  assert.equal(r.last, 'ERR NOSD No SD card');
  // 挿入 → 次の処理で自動的に認識
  await fw.ctl('sd insert');
  await fw.cardLoad(pattern(4));
  r = await fw.cmd('B');
  assert.match(r.last, /^OK sd/);
  assert.ok(fw.lines.includes('E SD 1'));
  assert.match((await fw.cmd('status')).last, /^STATUS sd=1/);
  await expectClean(fw);
  await fw.close();
});

test('処理の合間に SD を抜き差ししてもエラーにならない', async () => {
  const fw = await boot();
  await fw.cardLoad(pattern(6));
  assert.match((await fw.cmd('B')).last, /^OK sd/);

  // 抜く → 待機中に検出して状態が変わる
  await fw.ctl('sd remove');
  await fw.advance(2000);
  assert.ok(fw.lines.filter((l) => l === 'E SD 0').length >= 1);
  assert.match((await fw.cmd('status')).last, /^STATUS sd=0/);
  assert.equal((await fw.cmd('V')).last, 'ERR NOSD No SD card');

  // 挿す → すぐ使える
  await fw.ctl('sd insert');
  let r = await fw.cmd('V');
  assert.match(r.last, /^OK sd all 32768 bytes match/);

  // 抜き差しを素早く (検出の間隔より短く) 繰り返す。古い接続は使えないが、再マウントして使える
  await fw.ctl('sd remove');
  await fw.ctl('sd insert');
  r = await fw.cmd('V');
  assert.match(r.last, /^OK sd all 32768 bytes match/, r.last);
  await fw.ctl('sd swap');
  r = await fw.cmd('B');
  assert.match(r.last, /^OK sd/, r.last);

  // sdcheck
  await fw.ctl('sd remove');
  assert.equal((await fw.cmd('sdcheck')).last, 'ERR NOSD No SD card');
  await fw.ctl('sd insert');
  assert.match((await fw.cmd('sdcheck')).last, /^OK sdcheck/);

  // 抜き差しの後も MCP / SRAM が動く (SD.end() が共有 SPI を止めない)
  assert.match((await fw.cmd('R')).last, /^OK copy/);
  assert.match((await fw.cmd('X')).last, /^OK cmp/);
  await expectClean(fw);
  await fw.close();
});

test('処理の途中で SD を抜くと SDLOST になり、挿し直せば復帰する', async () => {
  for (const [cmd, label] of [['B', 'backup'], ['V', 'verify'], ['L', 'restore']]) {
    const fw = await boot();
    await fw.cardLoad(pattern(7));
    await fw.cmd('B');
    await fw.ctl('trigger P>=12000 => sd remove');
    const old = fs.readFileSync(sdfile(fw));
    const r = await fw.cmd(cmd);
    assert.equal(r.last, 'ERR SDLOST SD card removed', label);
    assert.ok(fs.readFileSync(sdfile(fw)).equals(old), label + ': 前のバックアップは変わらない');
    assert.ok(fw.lines.includes('E SD 0'), label);
    // 抜いたまま → NOSD。挿せば元通り
    assert.equal((await fw.cmd('V')).last, 'ERR NOSD No SD card', label);
    await fw.ctl('sd insert');
    assert.match((await fw.cmd('V')).last, /^OK sd|^NG sd/, label);
    assert.match((await fw.cmd('B')).last, /^OK sd/, label);
    assert.match((await fw.cmd('V')).last, /^OK sd all/, label);
    await expectClean(fw);
    await fw.close();
  }
});

test('中断 (!) できる。中断しても前のバックアップは壊れない', async () => {
  const fw = await boot();
  await fw.cardLoad(pattern(8));
  await fw.cmd('B');
  const before = fs.readFileSync(sdfile(fw));
  assert.equal(before.length, 32768);
  await fw.cardLoad(pattern(10));
  await fw.ctl('trigger P>=9000 => inject 21');
  let r = await fw.cmd('B');
  assert.match(r.last, /^ERR CANCEL/);
  assert.ok(fs.readFileSync(sdfile(fw)).equals(before), '前のバックアップが残っている');
  assert.ok(!fs.existsSync(sdfile(fw, 'FSU_TMP.TMP')), '一時ファイルが残っていない');
  // 中断しても次の処理はできる (カードは書き換えたので照合は不一致)
  r = await fw.cmd('V');
  assert.match(r.last, /^NG sd/);
  await fw.ctl('trigger P>=9000 => inject 21');
  r = await fw.cmd('R');
  assert.match(r.last, /^ERR CANCEL/);
  assert.match((await fw.cmd('status')).last, /busy=0/);
  // 待機中に届いた '!' は次のコマンドに混ざらない
  await fw.ctl('inject 21');
  assert.match((await fw.cmd('status')).last, /^STATUS/);
  await fw.close();
});

test('rd 中の中断', async () => {
  const fw = await boot();
  await fw.ctl('trigger D 0400 => inject 21');
  const r = await fw.cmd('rd f 0 32768');
  assert.match(r.last, /^ERR CANCEL/);
  assert.ok(r.lines.filter((l) => l.startsWith('D ')).length < 100);
  await fw.close();
});

test('旧 1 文字コマンドと CRLF', async () => {
  const fw = await boot();
  fw.send('?\r\n');
  await fw.advance(50);
  assert.ok(fw.lines.some((l) => l.startsWith('FUJISOKU SRAM Utility 2.8')));
  const r = await fw.cmd('e');
  assert.match(r.last, /^OK fill/);
  assert.ok((await fw.cardDump()).every((b) => b === 0xff));
  assert.match((await fw.cmd('Z')).last, /^ERR CMD/);
  await fw.close();
});

test('長すぎる行は ERR LINE、その次の行は普通に処理される', async () => {
  const fw = await boot();
  fw.send('x'.repeat(500) + '\n');
  await fw.advance(50);
  assert.ok(fw.lines.some((l) => /^ERR LINE/.test(l)));
  assert.match((await fw.cmd('status')).last, /^STATUS/);
  await fw.close();
});

test('USB が無くても (LCD だけで) 動く', async () => {
  const fw = await boot({ args: ['--no-usb'] });
  fw.send('status\n');
  await fw.advance(50);
  assert.equal(fw.lines.length, 0, 'USB 未接続のときは何も送らない');
  await fw.ctl('usb 1');
  assert.match((await fw.cmd('status')).last, /^STATUS/);
  await fw.close();
});

test('Web アプリと同じ手順 (64 バイトずつ wr、rd で読み戻し) で全域を書き換えられる', async () => {
  const fw = await boot({ args: ['--realtime'] });
  const pat = pattern(11);
  await fw.cardLoad(pattern(12));
  await fw.cmd('ui begin 65536 Write_from_PC');
  for (let a = 0; a < 0x8000; a += 64) {
    const r = await fw.cmd(`wr f ${a} ${pat.subarray(a, a + 64).toString('hex')}`, 5000);
    assert.match(r.last, /^OK wr/);
    if ((a & 0x7ff) === 0x7c0) assert.match((await fw.cmd(`ui prog ${a + 64} Writing_card`)).last, /^OK ui/);
  }
  assert.ok((await fw.cardDump()).equals(pat), '書き込み');
  const r = await fw.cmd('rd f 0 32768');
  const got = Buffer.concat(r.lines.filter((l) => l.startsWith('D ')).map((l) => Buffer.from(l.split(' ')[2], 'hex')));
  assert.ok(got.equals(pat), '読み出し');
  assert.match((await fw.cmd('ui end ok Written')).last, /^OK ui/);
  await expectClean(fw);
  await fw.close();
});

test('wr は 1 行 64 バイトまで', async () => {
  const fw = await boot();
  assert.match((await fw.cmd('wr f 0 ' + '00'.repeat(65))).last, /^ERR ARG/);
  await fw.close();
});

test('処理が終わるとアドレス・データ線を手放し、制御線はすべて非選択', async () => {
  const fw = await boot();
  const idle = 'bus addr_dir=FFFF data_dir=FF ctrl=3F ctrl_dir=00';
  assert.equal(await fw.ctl('bus'), idle, '起動直後');
  for (const c of ['B', 'V', 'R', 'X', 'rd f 0 16', 'wr f 0 00', 'fill f 0 4 0', 'sd ls']) {
    await fw.cmd(c);
    assert.equal(await fw.ctl('bus'), idle, c);
  }
  await fw.ctl('trigger P>=5000 => inject 21');
  await fw.cmd('L');
  assert.equal(await fw.ctl('bus'), idle, '中断のあと');
  await fw.close();
});

test('カードの検出 (/CARD DET = D2): 無ければ ERR NOCARD、抜き差しは E CARD で知らせる', async () => {
  let fw = await boot({ args: ['--no-card'] });
  assert.ok(fw.lines.some((l) => /card none\)/.test(l)), '起動メッセージ');
  for (const c of ['rd f 0 8', 'wr f 0 00', 'fill f 0 1 0', 'copy f s', 'copy s f', 'cmp', 'erase f', 'R', 'C', 'X', 'e', 'B', 'L', 'V', 'sd save f']) {
    assert.equal((await fw.cmd(c)).last, 'ERR NOCARD No card in the slot', c);
  }
  // カードを使わない操作はできる
  assert.match((await fw.cmd('rd s 0 1')).last, /^OK rd/);
  assert.match((await fw.cmd('erase s')).last, /^OK fill/);
  assert.match((await fw.cmd('status')).last, / card=0 batt=-$/);
  // 挿す: 50 ms のチャタリング除去のあと E CARD 1、続けて電池電圧
  await fw.ctl('cardpresent 1');
  await fw.advance(400);
  assert.ok(fw.lines.includes('E CARD 1'));
  assert.ok(fw.lines.includes('E BATT 3000'), fw.lines.slice(-5).join('|'));
  assert.match((await fw.cmd('status')).last, / card=1 batt=3000$/);
  assert.match((await fw.cmd('rd f 0 1')).last, /^OK rd 1/);
  // 抜く
  await fw.ctl('cardpresent 0');
  await fw.advance(200);
  assert.ok(fw.lines.includes('E CARD 0'));
  await fw.close();

  // 処理の途中で抜かれたら止める
  fw = await boot();
  await fw.ctl('trigger P>=8192 => cardpresent 0');
  assert.match((await fw.cmd('R')).last, /^ERR NOCARD Card removed at 0x[0-9A-F]{4}$/);
  await fw.ctl('cardpresent 1');
  await fw.advance(200);
  await fw.ctl('trigger P>=4096 => cardpresent 0');
  assert.match((await fw.cmd('sd save f cut')).last, /^ERR NOCARD Card removed/);
  assert.equal(await fw.ctl('bus'), 'bus addr_dir=FFFF data_dir=FF ctrl=3F ctrl_dir=00', '止めたあとバスは待機状態');
  await fw.close();
});

test('電池電圧 (BATT-DET = A2): 50 mV 以上の変化で E BATT、1.50 V 以下で「少ない」、0.50 V 以下で「電池なし」', async () => {
  const fw = await boot({ args: ['--batt-mv', '2950'] });
  await fw.advance(800);
  assert.match((await fw.cmd('status')).last, / batt=2950$/);
  let lcd = lcdText((await fw.cmd('lcd')).last);
  assert.equal(lcd[1], 'Card:OK   Bat:2.95V ');
  assert.equal(lcd[3], 'Last:---           #', '4 行目の右下は署名 (自作文字)');
  await fw.ctl('batt 2930');  // 20 mV の変化は送らない
  await fw.advance(800);
  assert.ok(!fw.lines.includes('E BATT 2930'));
  await fw.ctl('batt 1490');  // 1.50 V 以下は「少ない」 (ちょうど 1.50 V は 12 ビットの丸めで 1.501 V になるので、少し下で試す)
  await fw.advance(800);
  assert.ok(fw.lines.includes('E BATT 1490'));
  lcd = lcdText((await fw.cmd('lcd')).last);
  assert.equal(lcd[1], 'Card:OK   Bat:1.49V!');
  assert.equal(lcd[3], '! Card battery low #');
  await fw.ctl('batt 1540');  // 境目をまたいだら 50 mV 未満でも送る
  await fw.advance(800);
  assert.ok(fw.lines.includes('E BATT 1540'));
  assert.match(lcdText((await fw.cmd('lcd')).last)[3], /^Last:/);
  // 電池なし: 1 MΩ のプルダウンで 0 V 近く (作者の基板では 0.24 V)。0.50 V 以下は「電池なし」
  await fw.ctl('batt 240');
  await fw.advance(800);
  assert.ok(fw.lines.some((l) => /^E BATT 24[01]$/.test(l)), fw.lines.slice(-5).join('|'));  // 12 ビットの丸めで 240 か 241
  lcd = lcdText((await fw.cmd('lcd')).last);
  assert.equal(lcd[1], 'Card:OK   Bat:NONE! ');
  assert.equal(lcd[3], '! No card battery  #');
  await fw.ctl('batt 490');  // 0.50 V 以下は電池なし
  await fw.advance(800);
  assert.equal(lcdText((await fw.cmd('lcd')).last)[1], 'Card:OK   Bat:NONE! ');
  await fw.ctl('batt 520');  // 境目をまたいだら送る。0.52 V は「少ない」
  await fw.advance(800);
  assert.ok(fw.lines.includes('E BATT 520'));
  lcd = lcdText((await fw.cmd('lcd')).last);
  assert.equal(lcd[1], 'Card:OK   Bat:0.52V!');
  assert.equal(lcd[3], '! Card battery low #');
  await fw.close();
});


// ---- オプション部品 ----
const lcdText = (line) => {
  const h = line.slice(4);
  const rows = [];
  for (let r = 0; r < 4; r++) rows.push(Buffer.from(h.slice(r * 40, r * 40 + 40), 'hex').toString('latin1').replace(/[\x00-\x0f]/g, '#'));
  return rows;
};

test('LCD が見つからなくても動き、表示内容は lcd コマンドで取れる (USE_LCD=1)', async () => {
  const fw = await boot({ args: ['--no-lcd'] });
  assert.ok(fw.lines.some((l) => l.startsWith('FUJISOKU SRAM Utility 2.8 ready (LCD none')));
  assert.match((await fw.cmd('hello')).last, /lcd=none sram62256=0 sdcard=1 spisram=ok carddet=1 battdet=1 battlow=1500 battnone=500$/);
  assert.match((await fw.cmd('status')).last, / lcd=0 /);
  assert.match((await fw.cmd('B')).last, /^OK sd/);
  const r = await fw.cmd('lcd');
  assert.match(lcdText(r.last)[0], /^# BACKUP +DONE$/);
  await expectClean(fw);
  await fw.close();
});

test('USE_LCD=0 でビルドしても同じように動く (LCD のコードなし)', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_nolcd') });
  assert.match((await fw.cmd('hello')).last, /lcd=none/);
  await fw.cardLoad(pattern(21));
  assert.match((await fw.cmd('B')).last, /^OK sd/);
  assert.match((await fw.cmd('V')).last, /^OK sd all/);
  assert.match((await fw.cmd('ui begin 100 Test')).last, /^OK ui/);
  assert.match((await fw.cmd('ui end ok Done')).last, /^OK ui/);
  assert.match(lcdText((await fw.cmd('lcd')).last)[0], /^# Test +DONE$/);
  const d = await fw.diag();
  assert.equal(d.lcd_writes, 0, 'LCD に何も送らない');
  await expectClean(fw);
  await fw.close();
});

test('62256 を使わない設定 (既定) では対象 p は ERR DISABLED', async () => {
  const fw = await boot();
  for (const c of ['rd p 0 1', 'wr p 0 00', 'copy f p', 'cmp f p', 'test p', 'erase p', 'sd save p']) {
    assert.equal((await fw.cmd(c)).last, 'ERR DISABLED 62256 is not used (USE_SRAM62256 0)', c);
  }
  assert.match((await fw.cmd('cmp f f')).last, /^ERR ARG/);
  assert.match((await fw.cmd('cmp f s')).last, /^(OK|NG) cmp/);
  await fw.close();
});

test('62256 オプション (USE_SRAM62256=1): 読み書き・コピー・比較・SD 保存', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_62256') });
  assert.ok(fw.lines.some((l) => l.includes('62256 on')));
  assert.match((await fw.cmd('hello')).last, /sram62256=1 sdcard=1 spisram=ok carddet=1 battdet=1 battlow=1500 battnone=500$/);
  const pat = pattern(22);
  await fw.cardLoad(pat);
  assert.match((await fw.cmd('wr p 0x10 A1B2C3')).last, /^OK wr/);
  assert.equal((await fw.cmd('rd p 0x10 3')).lines[0], 'D 0010 A1B2C3');
  assert.equal((await fw.cmd('rd p 0x8000 1')).last, 'ERR RANGE out of range', '62256 は 32KB まで');
  assert.match((await fw.cmd('copy f p')).last, /^OK copy/);
  const f = path.join(fw.tmp, 'r62.bin');
  await fw.ctl('ram62 dump ' + f);
  assert.ok(fs.readFileSync(f).equals(pat), '62256 にカードの内容が入る');
  assert.match((await fw.cmd('cmp f p')).last, /^OK cmp all 32768 bytes match/);
  await fw.ctl('poke card 7fff 00');
  assert.match((await fw.cmd('cmp f p')).last, /^NG cmp mismatches=1 first=0x7FFF/);
  assert.match((await fw.cmd('sd save p ram')).last, /^OK sd/);
  assert.ok(fs.readFileSync(path.join(fw.sdRoot, 'ram.bin')).equals(pat));
  assert.match((await fw.cmd('erase p')).last, /^OK fill/);
  // カードとの切り替え (B0 共用) でカードを壊していない
  const card = await fw.cardDump();
  assert.equal(card[0x7fff], 0x00);
  assert.ok(card.subarray(0, 0x7fff).equals(pat.subarray(0, 0x7fff)));
  await expectClean(fw);
  await fw.close();
});

test('62256 でのバス試験 (test p): 正常なら OK、線の固定やショートを見つける', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_62256') });
  let r = await fw.cmd('test p');
  assert.equal(r.last, 'OK test D0-D7 OK A0-A14 OK');
  assert.ok(r.lines.includes('T 62256 data_bad=0x00 addr_bad=0x0'));
  await fw.ctl('stuck data 2 0');
  r = await fw.cmd('test p');
  assert.match(r.last, /^NG test data bad=0x04/);
  await fw.ctl('stuck clear');
  await fw.ctl('stuck addr 5 1');
  r = await fw.cmd('test p');
  assert.match(r.last, /^NG test data bad=0x00 addr bad=0x[0-9A-F]+$/);
  assert.ok(parseInt(r.last.split('0x').pop(), 16) & (1 << 5), 'A5 が含まれる: ' + r.last);
  await fw.ctl('stuck clear');
  await fw.ctl('stuck addr 11 0');
  r = await fw.cmd('test p');
  assert.ok(parseInt(r.last.split('0x').pop(), 16) & (1 << 11), 'A11 が含まれる: ' + r.last);
  await fw.ctl('stuck clear');
  assert.equal((await fw.cmd('test p')).last, 'OK test D0-D7 OK A0-A14 OK');
  await expectClean(fw);
  await fw.close();
});

test('SPI SRAM を使わない設定 (USE_SPI_SRAM=0): 対象 s と R C X E S W T は ERR DISABLED、カードは使える', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_nospi') });
  assert.ok(fw.lines.some((l) => l.includes('SPI SRAM off')), fw.lines.join('|'));
  assert.match((await fw.cmd('hello')).last, /spisram=off carddet=0 battdet=0 battlow=1500 battnone=500$/);
  assert.match((await fw.cmd('status')).last, / spi=- /);
  const want = 'ERR DISABLED SPI SRAM is not used (USE_SPI_SRAM 0)';
  for (const c of ['R', 'C', 'X', 'E', 'S', 'W', 'T', 'rd s 0 1', 'wr s 0 00', 'fill s 0 1 0', 'copy f s', 'copy s f', 'cmp', 'cmp s f', 'erase s', 'test', 'test s']) {
    assert.equal((await fw.cmd(c)).last, want, c);
  }
  // カードの読み書き (Web アプリの手順) と消去は使える
  await fw.cardLoad(pattern(41));
  assert.match((await fw.cmd('wr f 0x10 A1B2')).last, /^OK wr/);
  assert.ok((await fw.cmd('rd f 0x10 2')).lines.includes('D 0010 A1B2'));
  assert.match((await fw.cmd('e')).last, /^OK fill/);
  assert.ok((await fw.cardDump()).every((b) => b === 0xff));
  await expectClean(fw);
  await fw.close();
});

test('SPI SRAM が見つからない (USE_SPI_SRAM=1): SPI SRAM を使う操作は ERR NOSPI、自己診断は実行して NG', async () => {
  const fw = await boot({ args: ['--no-spi-sram'] });
  assert.ok(fw.lines.some((l) => l.includes('SPI SRAM none')), fw.lines.join('|'));
  assert.match((await fw.cmd('hello')).last, /spisram=none carddet=1 battdet=1 battlow=1500 battnone=500$/);
  for (const c of ['R', 'C', 'X', 'E', 'S', 'W', 'rd s 0 1', 'copy f s', 'cmp', 'erase s', 'sd save s']) {
    assert.equal((await fw.cmd(c)).last, 'ERR NOSPI SPI SRAM not found', c);
  }
  assert.match((await fw.cmd('T')).last, /^NG test no response/);
  assert.match((await fw.cmd('rd f 0 1')).last, /^OK rd 1/);
  await fw.close();
});

test('SD カードを使わない設定 (USE_SD=0): SD のコマンドは ERR DISABLED、ほかは同じように動く', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_nosd') });
  assert.ok(fw.lines.some((l) => l.includes('SD off')), fw.lines.join('|'));
  assert.ok(!fw.lines.some((l) => /^E SD/.test(l)), 'SD の通知は出さない');
  assert.match((await fw.cmd('hello')).last, /sdcard=0 spisram=ok carddet=1 battdet=1 battlow=1500 battnone=500$/);
  assert.match((await fw.cmd('status')).last, /^STATUS sd=- busy=0/);
  for (const c of ['B', 'L', 'V', 'S', 'W', 'sdcheck', 'sd ls', 'sd check', 'sd save f', 'sd load f x v', 'sd verify f']) {
    assert.equal((await fw.cmd(c)).last, 'ERR DISABLED SD card is not used (USE_SD 0)', c);
  }
  // PC からの読み書き (Web アプリの手順) と SPI SRAM は使える
  const pat = pattern(31);
  await fw.cardLoad(pattern(32));
  for (let a = 0; a < 0x400; a += 64) assert.match((await fw.cmd(`wr f ${a} ${pat.subarray(a, a + 64).toString('hex')}`)).last, /^OK wr/);
  const rd = await fw.cmd('rd f 0 1024');
  const got = Buffer.concat(rd.lines.filter((l) => l.startsWith('D ')).map((l) => Buffer.from(l.split(' ')[2], 'hex')));
  assert.ok(got.equals(pat.subarray(0, 0x400)));
  assert.match((await fw.cmd('R')).last, /^OK copy/);
  assert.match((await fw.cmd('X')).last, /^OK cmp all/);
  // 表示は SD:off
  const lcd = lcdText((await fw.cmd('lcd')).last);
  await fw.advance(16000);
  assert.match(lcdText((await fw.cmd('lcd')).last)[2], /^SD:off +PC:OK/);
  assert.ok(lcd.length === 4);
  const d = await fw.diag();
  assert.equal(d.sd_ops, 0, 'SD カードに触れない');
  await expectClean(fw);
  await fw.close();
});

test('SD を抜いても挿しても、SD を使わない設定では何も起きない', async () => {
  const fw = await boot({ bin: path.join(OUT, 'fsu_sim_nosd') });
  await fw.ctl('sd remove');
  await fw.advance(3000);
  await fw.ctl('sd insert');
  await fw.advance(3000);
  assert.ok(!fw.lines.some((l) => /^E SD/.test(l)));
  assert.equal((await fw.diag()).sd_ops, 0);
  await fw.close();
});
