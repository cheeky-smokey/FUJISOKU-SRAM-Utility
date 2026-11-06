// Web アプリ (webapp/index.html) を Playwright の Chromium で開き、
// navigator.serial の代わりにホストシミュレータのファームウェアへつなぐ
const path = require('path');
const { chromium } = require('playwright');
const { Firmware } = require('./fw');

const APP = 'file://' + path.join(__dirname, '..', '..', 'webapp', 'index.html');

// ページ内に差し込む Web Serial の代役
const MOCK = () => {
  const listeners = [];
  let controller = null;
  const port = {
    readable: null,
    writable: null,
    async open() {
      this.readable = new ReadableStream({ start(c) { controller = c; } });
      this.writable = new WritableStream({ write(chunk) { window.__serialWrite(Array.from(chunk)); } });
      await window.__serialOpen();
    },
    async close() { window.__serialClose && window.__serialClose(); },
    getInfo() { return { usbVendorId: 0x2341, usbProductId: 0x0069 }; },
  };
  window.__serialPush = (b64) => {
    if (!controller) return;
    const s = atob(b64);
    const u8 = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
    controller.enqueue(u8);
  };
  window.__serialEnd = () => { if (controller) { try { controller.error(new Error('device lost')); } catch (e) {} } };
  Object.defineProperty(navigator, 'serial', {
    configurable: true,
    value: {
      async requestPort() { return port; },
      async getPorts() { return [port]; },
      addEventListener(t, f) { listeners.push([t, f]); },
      removeEventListener() {},
    },
  });
  // ファイル保存はダウンロードで受ける (保存ダイアログは自動テストで操作できない)
  window.showSaveFilePicker = undefined;
};

async function openApp(opts = {}) {
  const browser = opts.browser || (await chromium.launch());
  const context = await browser.newContext({ acceptDownloads: true, viewport: opts.viewport || { width: 1360, height: 1000 }, colorScheme: opts.colorScheme || 'light', deviceScaleFactor: opts.deviceScaleFactor || 1 });
  const page = await context.newPage();
  const fw = new Firmware({ bin: opts.bin, args: ['--realtime', ...(opts.fwArgs || [])] });
  await fw.advance(50);
  page.on('pageerror', (e) => { (page.errors = page.errors || []).push(e); });
  page.on('console', (m) => { if (m.type() === 'error') (page.consoleErrors = page.consoleErrors || []).push(m.text()); });
  await page.exposeFunction('__serialWrite', (arr) => fw.send(Buffer.from(arr)));
  await page.exposeFunction('__serialOpen', () => true);
  await page.exposeFunction('__serialClose', () => true);
  if (!opts.noSerial) await page.addInitScript(MOCK);
  else await page.addInitScript(() => { try { delete Navigator.prototype.serial; } catch (e) {} });
  fw.p.stdout.on('data', (d) => {
    page.evaluate((b64) => window.__serialPush && window.__serialPush(b64), d.toString('base64')).catch(() => {});
  });
  await page.goto(APP);
  return { browser, context, page, fw, close: async () => { await fw.close(); if (!opts.browser) await browser.close(); } };
}

module.exports = { openApp, APP };
