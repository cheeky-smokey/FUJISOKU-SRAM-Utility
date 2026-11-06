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
  assert.equal(await page.textContent('#fwVer'), 'v2.1');
  assert.equal(await page.textContent('#lcdState'), 'あり (20×4)');
  assert.equal(await page.textContent('#ramState'), '使わない');
  assert.equal(await page.isHidden('#tools62256'), true);
  assert.equal(await page.textContent('#sdState'), '使えます');
  await page.waitForFunction(() => !!lcdBytes);
  const t = await lcdText(page);
  assert.equal(t[0], 'FUJISOKU SRAM   v2.1');
  assert.equal(t[1].trim(), 'SD:OK      PC:OK');
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
  assert.match(await page.textContent('#dropWrite'), /32,768 バイトのファイルを選んでください/);
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
  assert.match(await page.textContent('#resTools'), /D6: PASS/);
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
