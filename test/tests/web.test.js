// Web アプリの E2E テスト: Chromium + Web Serial の代役 + ホストシミュレータのファームウェア
const test = require('node:test');
const { before, after, afterEach } = test;
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');
const { openApp } = require('../lib/web');
const { Firmware } = require('../lib/fw');
const OUT = path.join(__dirname, '..', 'out');

const SHOTS = path.join(__dirname, '..', 'out', 'web');
fs.mkdirSync(SHOTS, { recursive: true });
let browser;
before(async () => { browser = await chromium.launch(); });
after(async () => { await browser.close(); });
afterEach(() => Firmware.closeAll());

const pattern = (seed) => {
  const b = Buffer.alloc(0x8000, 0xff);
  for (let i = 0; i < 0x3000; i++) b[i] = (i * 31 + (i >> 8) * 7 + seed) & 0xff;
  return b;
};
async function connected(opts = {}) {
  const app = await openApp({ browser, ...opts });
  if (opts.card) await app.fw.cardLoad(opts.card);
  await app.page.click('#btnConnect');
  await app.page.waitForSelector('#connState.on', { timeout: 15000 });
  await app.page.waitForFunction(() => !document.querySelector('#btnSave').disabled);
  return app;
}
const lcdText = (page) => page.evaluate(() => {
  if (!lcdBytes) return null;
  const rows = [];
  for (let r = 0; r < 4; r++) rows.push(Array.from(lcdBytes.slice(r * 20, r * 20 + 20), (b) => (b < 16 ? '#' : String.fromCharCode(b))).join(''));
  return rows;
});
async function waitResult(page, id, timeout = 90000) {
  await page.waitForSelector(`${id}.ok, ${id}.ng, ${id}.err, ${id}.info`, { timeout });
  const cls = await page.getAttribute(id, 'class');
  return { kind: cls.replace('result', '').trim(), text: (await page.textContent(id)).replace(/^[✓!×i]/, '') };
}
const noErrors = (page) => assert.deepEqual(page.errors || [], [], 'ページでエラーが起きた');
async function fileInput(page, sel, name, buf) {
  await page.setInputFiles(sel, { name, mimeType: 'application/octet-stream', buffer: buf });
}

test('接続前: 操作ボタンは押せず、LCD は未接続の表示', async () => {
  const app = await openApp({ browser });
  const { page } = app;
  assert.equal(await page.isHidden('#unsupported'), true);
  for (const id of ['#btnSave', '#btnSdSave', '#btnSdCheck']) assert.equal(await page.isDisabled(id), true, id);
  assert.equal(await page.isDisabled('#btnConnect'), false);
  assert.equal(await page.textContent('#connState'), '未接続');
  await page.screenshot({ path: path.join(SHOTS, 'start.png') });
  noErrors(page);
  await app.close();
});

test('Web Serial の無いブラウザ: 案内を出し、ファイル同士の比較はできる', async () => {
  const app = await openApp({ browser, noSerial: true });
  const { page } = app;
  assert.equal(await page.evaluate(() => 'serial' in navigator), false);
  assert.equal(await page.isHidden('#unsupported'), false);
  assert.equal(await page.isDisabled('#btnConnect'), true);
  await page.screenshot({ path: path.join(SHOTS, 'unsupported.png') });
  await page.click('#t-compare');
  await page.click('label:has(input[name="srcA"][value="file"])');
  assert.equal(await page.isChecked('input[name="srcA"][value="file"]'), true);
  const a = pattern(1), b = Buffer.from(a);
  b[0x10] ^= 0xff; b[0x2000] ^= 0x55;
  await fileInput(page, '#fileA', 'a.bin', a);
  await fileInput(page, '#fileB', 'b.bin', b);
  await page.click('#btnCompare');
  const r = await waitResult(page, '#resCompare');
  assert.equal(r.kind, 'ng');
  assert.match(r.text, /2 バイトが一致しません/);
  assert.match(r.text, /0x0010/);
  noErrors(page);
  await app.close();
});

test('接続: バージョン・SD の状態・本体 LCD の表示が出る', async () => {
  const app = await connected();
  const { page } = app;
  assert.equal(await page.textContent('#fwVer'), 'v2.6');
  assert.equal(await page.isHidden('#t-sd'), false);
  assert.equal(await page.textContent('#lcdState'), 'あり (20×4)');
  assert.equal(await page.textContent('#ramState'), '使わない');
  assert.equal(await page.isHidden('#tools62256'), true);
  assert.equal(await page.textContent('#sdState'), '使えます');
  await page.waitForFunction(() => !!lcdBytes);
  const t = await lcdText(page);
  assert.equal(t[0], 'FUJISOKU SRAM   v2.6');
  assert.equal(t[1].trim(), 'Card:OK   Bat:3.00V');
  assert.equal(t[2].trim(), 'SD:OK      PC:OK');
  await page.screenshot({ path: path.join(SHOTS, 'connected.png') });
  noErrors(page);
  await app.close();
});

test('PC に保存: 2 回読んで一致を確かめ、ダウンロードの中身はカードと同じ', async () => {
  const card = pattern(2);
  const app = await connected({ card });
  const { page } = app;
  const dl = page.waitForEvent('download', { timeout: 90000 });
  await page.click('#btnSave');
  await page.waitForSelector('#prog:not([hidden])');
  await page.waitForTimeout(800);
  await page.screenshot({ path: path.join(SHOTS, 'progress.png') });
  const d = await dl;
  assert.match(d.suggestedFilename(), /^fujisoku_\d{8}_\d{6}\.bin$/);
  const got = fs.readFileSync(await d.path());
  assert.ok(got.equals(card), 'ダウンロードした内容がカードと違う');
  const r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'ok');
  assert.match(r.text, /2 回の読み込みは一致しました/);
  assert.equal(await page.isHidden('#viewBody'), false);
  await page.waitForFunction(() => lcdBytes && String.fromCharCode(...lcdBytes.slice(0, 20)).includes('DONE'));
  assert.match((await lcdText(page))[0], /Card to PC +DONE$/);
  await page.screenshot({ path: path.join(SHOTS, 'saved.png'), fullPage: true });
  noErrors(page);
  await app.close();
});

test('PC に保存: 2 回の読み込みが違うときは保存せず、違う場所を見せる', async () => {
  const app = await connected({ card: pattern(3) });
  const { page, fw } = app;
  await page.evaluate(() => { window.__dl = 0; });
  page.on('download', () => page.evaluate(() => window.__dl++));
  await fw.ctl('glitch 1234');
  await page.click('#btnSave');
  const r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'err');
  assert.match(r.text, /2 回の読み込み結果が一致しません/);
  assert.equal(await page.evaluate(() => window.__dl), 0, '保存してしまった');
  assert.match(await page.textContent('#diffInfo'), /0x1234/);
  noErrors(page);
  await app.close();
});

test('カードに書き込み: 書き込み前の保存、変わった部分だけ書き込み、照合', async () => {
  const card = pattern(4);
  const app = await connected({ card });
  const { page, fw } = app;
  const img = Buffer.from(card);
  for (let i = 0x100; i < 0x140; i++) img[i] = 0x11;   // 64 バイト 1 つ
  img[0x7fff] = 0x22;                                    // 最後の 64 バイト
  await page.click('#t-write');
  await fileInput(page, '#fileWrite', 'pattern.bin', img);
  await page.waitForFunction(() => !document.querySelector('#btnWrite').disabled);
  const dl = page.waitForEvent('download', { timeout: 90000 });
  await page.click('#btnWrite');
  await page.waitForSelector('#dlg[open]');
  await page.screenshot({ path: path.join(SHOTS, 'confirm.png') });
  const wrBefore = fw.lines.filter((l) => l.startsWith('OK wr')).length;
  await page.click('#dlgYes');
  const backup = fs.readFileSync(await (await dl).path());
  assert.ok(backup.equals(card), '書き込み前の保存がカードと違う');
  const r = await waitResult(page, '#resWrite');
  assert.equal(r.kind, 'ok', r.text);
  assert.match(r.text, /書き込み、照合しました/);
  assert.match(r.text, /backup_\d{8}_\d{6}\.bin/);
  assert.ok((await fw.cardDump()).equals(img), 'カードの中身がファイルと違う');
  assert.equal(fw.lines.filter((l) => l.startsWith('OK wr')).length - wrBefore, 2, '変わった 2 か所だけ書くはず');
  noErrors(page);
  await app.close();
});

test('カードに書き込み: 全部書く (保存なし)、確認でやめれば書かない、大きさ違いは選べない', async () => {
  const app = await connected({ card: pattern(5) });
  const { page, fw } = app;
  await page.click('#t-write');
  await fileInput(page, '#fileWrite', 'short.bin', Buffer.alloc(1000));
  assert.equal(await page.isDisabled('#btnWrite'), true);
  assert.match(await page.textContent('#dropWrite'), /32,768〜262,144 バイト \(32KB〜256KB\) のファイルを選んでください/);
  await fileInput(page, '#fileWrite', 'huge.bin', Buffer.alloc(0x40000 + 1));
  assert.equal(await page.isDisabled('#btnWrite'), true);
  const img = pattern(6);
  await fileInput(page, '#fileWrite', 'p6.bin', img);
  await page.uncheck('#optBackup');
  await page.click('#btnWrite');
  await page.click('#dlgNo');
  await page.waitForTimeout(300);
  assert.ok((await fw.cardDump()).equals(pattern(5)), 'やめたのに書き込まれた');
  await page.click('#btnWrite');
  await page.click('#dlgYes');
  const r = await waitResult(page, '#resWrite');
  assert.equal(r.kind, 'ok', r.text);
  assert.ok((await fw.cardDump()).equals(img));
  noErrors(page);
  await app.close();
});

// 大きいカード用: 32KB ごとに中身が違う模様
const bigPattern = (size, seed) => {
  const b = Buffer.alloc(size);
  for (let i = 0; i < size; i++) b[i] = (i * 13 + (i >> 15) * 71 + seed) & 0xff;
  return b;
};
const pickSize = (page, label) => page.click(`#segReadSize label:has-text("${label}")`);

test('PC に保存: 読み込む大きさのボタン (既定 32KB)。64KB で読める。32KB のカードを大きく読むとくり返しを知らせる', async () => {
  const card = bigPattern(0x10000, 1);
  let app = await connected({ card, fwArgs: ['--card-kb', '64'] });
  let { page } = app;
  assert.equal(await page.isChecked('input[name="readSize"][value="32768"]'), true, '既定は 32KB');
  assert.equal(await page.locator('input[name="readSize"]:disabled').count(), 0);
  await pickSize(page, '64KB');
  assert.equal(await page.isChecked('input[name="readSize"][value="65536"]'), true);
  await page.uncheck('#optTwice');
  await page.screenshot({ path: path.join(SHOTS, 'read-size.png'), clip: await page.locator('#p-save').boundingBox() });
  let dl = page.waitForEvent('download', { timeout: 90000 });
  await page.click('#btnSave');
  let d = await dl;
  assert.match(d.suggestedFilename(), /^fujisoku_\d{8}_\d{6}_64KB\.bin$/);
  assert.ok(fs.readFileSync(await d.path()).equals(card), '64KB の内容がカードと違う');
  let r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'ok', r.text);
  assert.match(r.text, /65,536 バイト \(64KB\)/);
  assert.doesNotMatch(r.text, /くり返し/);
  assert.match(await page.textContent('#mapScale'), /1 つの穴 = 128 バイト、1 行 = 4KB/);
  noErrors(page);
  await app.close();

  // 32KB のカードを 64KB で読む: 後半は前半と同じ (上位のアドレス線が無い)
  app = await connected({ card: pattern(3) });
  ({ page } = app);
  await pickSize(page, '64KB');
  await page.uncheck('#optTwice');
  dl = page.waitForEvent('download', { timeout: 90000 });
  await page.click('#btnSave');
  d = await dl;
  const got = fs.readFileSync(await d.path());
  assert.equal(got.length, 0x10000);
  assert.ok(got.subarray(0x8000).equals(pattern(3)));
  r = await waitResult(page, '#resSave');
  assert.match(r.text, /先頭 32KB と同じ内容のくり返しです/);
  noErrors(page);
  await app.close();
});

test('カードに書き込み: 32KB より大きいファイルは「先頭 32KB だけ」か「すべて」かを選ぶ', async () => {
  const orig = bigPattern(0x10000, 2);
  const img = bigPattern(0x10000, 9);
  const app = await connected({ card: orig, fwArgs: ['--card-kb', '64'] });
  const { page, fw } = app;
  await page.click('#t-write');
  await fileInput(page, '#fileWrite', 'big.bin', img);
  await page.waitForFunction(() => /65,536 バイト \(64KB\)/.test(document.querySelector('#dropWrite').textContent));
  await page.uncheck('#optBackup');
  // やめる
  await page.click('#btnWrite');
  await page.waitForSelector('#dlg[open]');
  assert.match(await page.textContent('#dlgTitle'), /32KB より大きいファイルです/);
  assert.equal(await page.isVisible('#dlgAlt'), true);
  assert.equal(await page.textContent('#dlgYes'), 'すべて書き込む (64KB)');
  await page.screenshot({ path: path.join(SHOTS, 'confirm-big.png') });
  await page.click('#dlgNo');
  await page.waitForTimeout(300);
  assert.ok((await fw.cardDump()).equals(orig), 'やめたのに書き込まれた');
  // 先頭 32KB だけ
  await page.click('#btnWrite');
  await page.click('#dlgAlt');
  let r = await waitResult(page, '#resWrite');
  assert.equal(r.kind, 'ok', r.text);
  assert.match(r.text, /先頭 32,768 バイト \(32KB\) を書き込みました/);
  let card = await fw.cardDump();
  assert.ok(card.subarray(0, 0x8000).equals(img.subarray(0, 0x8000)), '先頭 32KB が書かれていない');
  assert.ok(card.subarray(0x8000).equals(orig.subarray(0x8000)), '32KB より後ろが書き換わった');
  // すべて
  await page.click('#btnWrite');
  await page.click('#dlgYes');
  r = await waitResult(page, '#resWrite');
  assert.equal(r.kind, 'ok', r.text);
  assert.match(r.text, /65,536 バイトすべて一致/);
  card = await fw.cardDump();
  assert.ok(card.equals(img), '64KB すべてが書かれていない');
  // 比較: カードはファイルと同じ大きさだけ読む
  await page.click('#t-compare');
  await fileInput(page, '#fileB', 'big.bin', img);
  await page.click('#btnCompare');
  await page.waitForFunction(() => /すべて一致/.test(document.querySelector('#resCompare').textContent), null, { timeout: 90000 });
  assert.match(await page.textContent('#resCompare'), /65,536 バイトすべて同じ/);
  noErrors(page);
  await app.close();
});

test('カードに書き込み: 32KB のカードに 64KB を「すべて」書くと、照合で違いを知らせる', async () => {
  const app = await connected({ card: pattern(1) });
  const { page } = app;
  await page.click('#t-write');
  await fileInput(page, '#fileWrite', 'big.bin', bigPattern(0x10000, 5));
  await page.uncheck('#optBackup');
  await page.click('#btnWrite');
  await page.click('#dlgYes');
  const r = await waitResult(page, '#resWrite');
  assert.equal(r.kind, 'ng', r.text);
  assert.match(r.text, /カードの容量が書き込んだ大きさより小さく/);
  noErrors(page);
  await app.close();
});

test('比較: カードとファイル。違いはマップと「次の違い」で確かめられる', async () => {
  const card = pattern(7);
  const app = await connected({ card });
  const { page } = app;
  const f = Buffer.from(card);
  f[0x0042] ^= 1; f[0x6000] = 0x00;
  await page.click('#t-compare');
  await fileInput(page, '#fileB', 'old.bin', f);
  await page.click('#btnCompare');
  const r = await waitResult(page, '#resCompare');
  assert.equal(r.kind, 'ng');
  assert.match(r.text, /2 バイトが一致しません/);
  assert.match(await page.textContent('#diffInfo'), /違い 1 \/ 2: 0x0042/);
  await page.click('#btnNextDiff');
  assert.match(await page.textContent('#diffInfo'), /違い 2 \/ 2: 0x6000/);
  assert.equal(await page.locator('#hexRows span.d').count() >= 1, true);
  await page.screenshot({ path: path.join(SHOTS, 'compare.png'), fullPage: true });
  // 一致する場合
  await fileInput(page, '#fileB', 'same.bin', card);
  await page.click('#btnCompare');
  await page.waitForFunction(() => /すべて一致/.test(document.querySelector('#resCompare').textContent), null, { timeout: 90000 });
  noErrors(page);
  await app.close();
});

test('SD カード: 一覧・保存・照合・書き込み・ファイル名の確認', async () => {
  const card = pattern(8);
  const app = await connected({ card });
  const { page, fw } = app;
  await page.click('#t-sd');
  await page.fill('#sdName', 'bad name!');
  await page.click('#btnSdSave');
  assert.match((await waitResult(page, '#resSd')).text, /ファイル名が使えません/);
  await page.fill('#sdName', 'pattern1');
  await page.click('#btnSdSave');
  let r = await waitResult(page, '#resSd');
  assert.equal(r.kind, 'ok', r.text);
  assert.ok(fs.readFileSync(path.join(fw.sdRoot, 'pattern1.bin')).equals(card));
  await page.waitForSelector('#sdFiles tr.pick');
  assert.match(await page.textContent('#sdFiles'), /pattern1\.bin/);
  await page.click('#btnSdVerify');
  await page.waitForFunction(() => /すべて一致/.test(document.querySelector('#resSd').textContent), null, { timeout: 60000 });
  await fw.ctl('poke card 10 00');
  await page.click('#btnSdVerify');
  await page.waitForFunction(() => /一致しません/.test(document.querySelector('#resSd').textContent), null, { timeout: 60000 });
  assert.match(await page.textContent('#resSd'), /0x0010/);
  await page.click('#btnSdLoad');
  await page.click('#dlgYes');
  await page.waitForFunction(() => /書き込み、照合しました/.test(document.querySelector('#resSd').textContent), null, { timeout: 90000 });
  assert.ok((await fw.cardDump()).equals(card));
  // 一覧の行を押すとファイル名に入る
  fs.writeFileSync(path.join(fw.sdRoot, 'other.bin'), card);
  await page.click('#btnSdList');
  await page.click('#sdFiles tr[data-name="other"]');
  assert.equal(await page.inputValue('#sdName'), 'other');
  await page.screenshot({ path: path.join(SHOTS, 'sd.png'), fullPage: true });
  noErrors(page);
  await app.close();
});

test('SD カード: 抜くと「入っていません」、挿して確認すると戻る。処理中に抜くと分かりやすい案内', async () => {
  const app = await connected({ card: pattern(9) });
  const { page, fw } = app;
  await page.click('#t-sd');
  await fw.ctl('sd remove');
  await page.waitForFunction(() => document.querySelector('#sdState').textContent === '入っていません', null, { timeout: 20000 });
  await page.click('#btnSdSave');
  let r = await waitResult(page, '#resSd');
  assert.equal(r.kind, 'err');
  assert.match(r.text, /SD カードが見つかりません/);
  await fw.ctl('sd insert');
  await page.click('#btnSdCheck');
  await page.waitForFunction(() => document.querySelector('#sdState').textContent === '使えます', null, { timeout: 20000 });
  await fw.ctl('trigger P>=10000 => sd remove');
  await page.click('#btnSdSave');
  await page.waitForFunction(() => /処理の途中で SD カードが抜かれました/.test(document.querySelector('#resSd').textContent), null, { timeout: 60000 });
  assert.equal(await page.textContent('#sdState'), '入っていません');
  // 挿し直せばすぐ使える
  await fw.ctl('sd insert');
  await page.click('#btnSdSave');
  r = await waitResult(page, '#resSd');
  assert.equal(r.kind, 'ok', r.text);
  noErrors(page);
  await app.close();
});

test('SPI SRAM・診断: コピー・比較・自己診断・消去 (確認あり)', async () => {
  const card = pattern(10);
  const app = await connected({ card });
  const { page, fw } = app;
  await page.click('#t-tools');
  await page.click('.action[data-cmd="copy f s"]');
  assert.equal((await waitResult(page, '#resTools')).kind, 'ok');
  await page.click('.action[data-cmd="cmp"]');
  await page.waitForFunction(() => /すべて一致/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  await page.click('.action[data-cmd="test"]');
  await page.waitForFunction(() => /自己診断/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  assert.match(await page.textContent('#resTools'), /自己診断: 異常はありません/);
  assert.match(await page.textContent('#resTools'), /T mode=40/);
  await page.click('.action[data-cmd="erase f"]');
  await page.click('#dlgNo');
  await page.waitForTimeout(200);
  assert.ok((await fw.cardDump()).equals(card));
  await page.click('.action[data-cmd="erase f"]');
  await page.click('#dlgYes');
  await page.waitForFunction(() => /完了しました/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  assert.ok((await fw.cardDump()).every((b) => b === 0xff));
  noErrors(page);
  await app.close();
});

test('中断: 読み込み中に中断でき、その後もすぐ次の操作ができる', async () => {
  const app = await connected({ card: pattern(11) });
  const { page, fw } = app;
  await page.click('#btnSave');
  await page.waitForFunction(() => parseInt(document.querySelector('#progPct').textContent) >= 10, null, { timeout: 60000 });
  await page.click('#btnCancel');
  const r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'info');
  assert.match(r.text, /中断しました/);
  await page.waitForFunction(() => lcdBytes && /CANCELED/.test(String.fromCharCode(...lcdBytes.slice(0, 20))), null, { timeout: 10000 });
  // 次の操作
  await page.click('#t-tools');
  await page.click('.action[data-cmd="cmp"]');
  await waitResult(page, '#resTools');
  noErrors(page);
  await app.close();
});

test('接続が切れたら未接続に戻り、本体は 8 秒後に待機へ戻る', async () => {
  const app = await connected({ card: pattern(12) });
  const { page, fw } = app;
  await page.click('#btnSave');
  await page.waitForFunction(() => parseInt(document.querySelector('#progPct').textContent) >= 5, null, { timeout: 60000 });
  await page.evaluate(() => window.__serialEnd());
  const r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'err');
  assert.match(r.text, /接続が切れました/);
  assert.equal(await page.textContent('#connState'), '未接続');
  const lost = fw.waitLine((l) => l === 'E UI timeout', 30000);
  await fw.advance(9000);
  await lost;
  noErrors(page);
  await app.close();
});

test('コマンド入力とログ', async () => {
  const app = await connected();
  const { page } = app;
  await page.click('#logBox summary');
  await page.fill('#cmdInput', 'status');
  await page.press('#cmdInput', 'Enter');
  await page.waitForFunction(() => /STATUS sd=1/.test(document.querySelector('#log').textContent));
  assert.match(await page.textContent('#log'), /→ status/);
  noErrors(page);
  await app.close();
});

test('LCD なし・62256 ありの本体: LCD は「なし」と出て、62256 の操作が使える', async () => {
  const app = await connected({ bin: path.join(OUT, 'fsu_sim_62256'), fwArgs: ['--no-lcd'], card: pattern(14) });
  const { page, fw } = app;
  assert.equal(await page.textContent('#lcdState'), 'なし (表示はここだけ)');
  assert.equal(await page.textContent('#ramState'), '使う');
  await page.waitForFunction(() => !!lcdBytes);  // LCD が無くても本体の表示内容は届く
  await page.click('#t-tools');
  assert.equal(await page.isHidden('#tools62256'), false);
  await page.click('.action[data-cmd="test p"]');
  await page.waitForFunction(() => /バス試験: 異常はありません/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  await fw.ctl('stuck addr 3 1');
  await page.click('.action[data-cmd="test p"]');
  await page.waitForFunction(() => /異常があります/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  assert.match(await page.textContent('#resTools'), /A3/);
  await fw.ctl('stuck clear');
  await page.click('.action[data-cmd="copy f p"]');
  await page.waitForFunction(() => /完了しました/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  await page.click('.action[data-cmd="cmp f p"]');
  await page.waitForFunction(() => /すべて一致/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  await page.screenshot({ path: path.join(SHOTS, 'tools62256.png'), fullPage: true });
  const d = await fw.diag();
  assert.equal(d.lcd_absent_writes, 0);
  noErrors(page);
  await app.close();
});

test('SD カードを使わない本体: SD のタブと案内を隠し、PC への保存と書き込みはできる', async () => {
  const card = pattern(15);
  const app = await connected({ bin: path.join(OUT, 'fsu_sim_nosd'), card });
  const { page, fw } = app;
  assert.equal(await page.textContent('#sdState'), '使わない');
  assert.equal(await page.isHidden('#t-sd'), true);
  assert.equal(await page.isHidden('#sdHelp'), true);
  // 矢印キーで隠したタブに移らない
  await page.focus('#t-compare');
  await page.keyboard.press('ArrowRight');
  assert.equal(await page.getAttribute('#t-tools', 'aria-selected'), 'true');
  await page.click('#t-save');
  const dl = page.waitForEvent('download', { timeout: 90000 });
  await page.click('#btnSave');
  assert.ok(fs.readFileSync(await (await dl).path()).equals(card));
  assert.equal((await waitResult(page, '#resSave')).kind, 'ok');
  const img = Buffer.from(card); img[0x200] ^= 0xff;
  await page.click('#t-write');
  await page.uncheck('#optBackup');
  await fileInput(page, '#fileWrite', 'img.bin', img);
  await page.click('#btnWrite');
  await page.click('#dlgYes');
  assert.equal((await waitResult(page, '#resWrite')).kind, 'ok');
  assert.ok((await fw.cardDump()).equals(img));
  await page.waitForFunction(() => lcdBytes && /SD:off/.test(String.fromCharCode(...lcdBytes.slice(40, 60))), null, { timeout: 30000 });
  await page.screenshot({ path: path.join(SHOTS, 'nosd.png'), fullPage: true });
  // 切断すると SD のタブは戻る
  await page.click('#btnConnect');
  await page.waitForFunction(() => document.querySelector('#connState').textContent === '未接続');
  assert.equal(await page.isHidden('#t-sd'), false);
  noErrors(page);
  await app.close();
});

test('SPI SRAM: 使わない設定なら操作を隠し、見つからなければ自己診断だけ出す', async () => {
  // USE_SPI_SRAM=0
  let app = await connected({ bin: path.join(OUT, 'fsu_sim_nospi'), card: pattern(12) });
  let { page } = app;
  assert.equal(await page.textContent('#spiState'), '使わない');
  await page.click('#t-tools');
  assert.match(await page.textContent('#t-tools'), /消去・診断/);
  for (const c of ['copy f s', 'copy s f', 'cmp', 'erase s', 'test']) assert.equal(await page.isVisible(`.action[data-cmd="${c}"]`), false, c);
  assert.equal(await page.isVisible('.action[data-cmd="erase f"]'), true, 'カードの消去は使える');
  await page.screenshot({ path: path.join(SHOTS, 'nospi.png'), clip: await page.locator('#p-tools').boundingBox() });
  noErrors(page);
  await app.close();

  // USE_SPI_SRAM=1 だが見つからない
  app = await connected({ fwArgs: ['--no-spi-sram'], card: pattern(13) });
  ({ page } = app);
  assert.equal(await page.textContent('#spiState'), '見つかりません');
  await page.click('#t-tools');
  assert.equal(await page.isVisible('#spiMissing'), true);
  assert.equal(await page.isVisible('.action[data-cmd="copy f s"]'), false);
  assert.equal(await page.isVisible('.action[data-cmd="test"]'), true);
  await page.click('.action[data-cmd="test"]');
  await page.waitForFunction(() => /自己診断/.test(document.querySelector('#resTools').textContent), null, { timeout: 60000 });
  assert.match(await page.textContent('#resTools'), /応答がありません/);
  assert.equal(await page.textContent('#spiState'), '見つかりません');
  noErrors(page);
  await app.close();
});

test('カードと電池: 状態を表示し、1.00 V 以下で警告、抜くと NOCARD の案内', async () => {
  const app = await connected({ card: pattern(14), fwArgs: ['--batt-mv', '2950'] });
  const { page, fw } = app;
  assert.equal(await page.textContent('#cardState'), '入っています');
  await page.waitForFunction(() => document.querySelector('#battState').textContent === '2.95 V', null, { timeout: 10000 });
  assert.equal(await page.isVisible('#battAlert'), false);
  // 電池が弱る
  await fw.ctl('batt 900');
  await page.waitForFunction(() => /0\.90 V \(少ない\)/.test(document.querySelector('#battState').textContent), null, { timeout: 10000 });
  assert.equal(await page.isVisible('#battAlert'), true);
  assert.match(await page.textContent('#battAlert'), /電池が少なくなっています \(0\.90 V\)/);
  await page.waitForFunction(() => lcdBytes && /Bat:0\.90V!/.test(String.fromCharCode(...lcdBytes.slice(20, 40))), null, { timeout: 10000 });
  await page.screenshot({ path: path.join(SHOTS, 'batt-low.png'), fullPage: true });
  // カードを抜く
  await fw.ctl('cardpresent 0');
  await page.waitForFunction(() => document.querySelector('#cardState').textContent === '入っていません', null, { timeout: 10000 });
  assert.equal(await page.textContent('#battState'), '—');
  assert.equal(await page.isVisible('#battAlert'), false);
  await page.uncheck('#optTwice');
  await page.click('#btnSave');
  const r = await waitResult(page, '#resSave');
  assert.equal(r.kind, 'err', r.text);
  assert.match(r.text, /カードが入っていません/);
  noErrors(page);
  await app.close();
});

test('カード検出・電池を使わない本体 (USE_CARD_DET=0, USE_BATT_DET=0)', async () => {
  const app = await connected({ bin: path.join(OUT, 'fsu_sim_nospi'), card: pattern(15) });
  const { page } = app;
  assert.equal(await page.textContent('#cardState'), '検出しない');
  assert.equal(await page.textContent('#battState'), '測らない');
  noErrors(page);
  await app.close();
});

test('ファイル欄: × で外せる。別のファイルや同じファイルを選び直せる', async () => {
  const app = await connected({ card: pattern(21) });
  const { page } = app;
  // ファイルを選び、欄の表示が変わるまで待つ (読み込みは非同期)
  const pick = async (input, drop, name, buf) => {
    await fileInput(page, input, name, buf);
    await page.waitForFunction(([d, n]) => document.querySelector(d + ' .name').textContent === n, [drop, name]);
  };
  await page.click('#t-write');
  assert.equal(await page.isVisible('#dropWrite .drop-clear'), false, '選ぶ前は × を出さない');
  await pick('#fileWrite', '#dropWrite', 'one.bin', pattern(22));
  assert.match(await page.textContent('#dropWrite .name'), /one\.bin/);
  assert.equal(await page.isDisabled('#btnWrite'), false);
  assert.equal(await page.isVisible('#dropWrite .hint'), true);
  // 別のファイルに替える
  await pick('#fileWrite', '#dropWrite', 'two.bin', pattern(23));
  assert.match(await page.textContent('#dropWrite .name'), /two\.bin/);
  await page.screenshot({ path: path.join(SHOTS, 'drop-selected.png'), clip: await page.locator('#dropWrite').boundingBox() });
  // × で外す: ファイル選択の画面は開かず、元の表示に戻る
  let chooser = false;
  page.once('filechooser', () => { chooser = true; });
  await page.click('#dropWrite .drop-clear');
  await page.waitForTimeout(200);
  assert.equal(chooser, false, '× でファイル選択が開いた');
  assert.equal(await page.textContent('#dropWrite .name'), '.bin ファイルを選ぶ');
  assert.equal(await page.isDisabled('#btnWrite'), true);
  assert.equal(await page.isVisible('#dropWrite .drop-clear'), false);
  // 同じファイルをもう一度選べる
  await pick('#fileWrite', '#dropWrite', 'two.bin', pattern(23));
  assert.match(await page.textContent('#dropWrite .name'), /two\.bin/);
  assert.equal(await page.isDisabled('#btnWrite'), false);
  // 比較の欄も同じ
  await page.click('#t-compare');
  await pick('#fileB', '#dropB', 'b.bin', pattern(24));
  assert.equal(await page.isDisabled('#btnCompare'), false);
  await page.click('#dropB .drop-clear');
  assert.equal(await page.isDisabled('#btnCompare'), true);
  assert.equal(await page.textContent('#dropB .name'), 'ファイル B を選ぶ');
  noErrors(page);
  await app.close();
});

test('見た目: ダーク表示とスマートフォン幅', async () => {
  const card = pattern(13);
  let app = await connected({ card, colorScheme: 'dark' });
  await app.page.click('#t-compare');
  const f = Buffer.from(card); for (let i = 0x2000; i < 0x2100; i++) f[i] ^= 0x0f;
  await fileInput(app.page, '#fileB', 'old.bin', f);
  await app.page.click('#btnCompare');
  await waitResult(app.page, '#resCompare');
  await app.page.screenshot({ path: path.join(SHOTS, 'dark.png'), fullPage: true });
  noErrors(app.page);
  await app.close();
  app = await connected({ card, viewport: { width: 390, height: 844 } });
  await app.page.screenshot({ path: path.join(SHOTS, 'mobile.png'), fullPage: true });
  const overflow = await app.page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth + 1);
  assert.equal(overflow, false, '横にはみ出している');
  noErrors(app.page);
  await app.close();
});
