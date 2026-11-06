// LCD のスナップショット (PPM P6) を扱う。PNG への変換は python の Pillow を使う
const { execFileSync } = require('child_process');
const fs = require('fs');

function ppmToPng(ppm, png, scale = 1) {
  execFileSync('python3', ['-c', `
import sys
from PIL import Image
im = Image.open(sys.argv[1])
s = int(sys.argv[3])
if s != 1: im = im.resize((im.width*s, im.height*s), Image.NEAREST)
im.save(sys.argv[2])
`, ppm, png, String(scale)]);
}

function readPpm(file) {
  const b = fs.readFileSync(file);
  // "P6\\n240 320\\n255\\n" の後ろに RGB
  let off = 0;
  const tok = [];
  while (tok.length < 4) {
    let s = '';
    while (b[off] !== 0x0a && b[off] !== 0x20) s += String.fromCharCode(b[off++]);
    off++;
    tok.push(s);
  }
  return { w: +tok[1], h: +tok[2], data: b.subarray(off) };
}
const pixel = (img, x, y) => [...img.data.subarray((y * img.w + x) * 3, (y * img.w + x) * 3 + 3)];
const near = (a, b, tol = 10) => a.every((v, i) => Math.abs(v - b[i]) <= tol);

module.exports = { ppmToPng, readPpm, pixel, near };
