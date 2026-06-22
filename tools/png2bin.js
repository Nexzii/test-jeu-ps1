// PNG -> raw 16-bit BGR555 binary for PS1 textures (no CLUT, direct color).
// Usage: node tools/png2bin.js <in.png> <out.bin> <W> <H>
const fs = require('fs');
const zlib = require('zlib');

const inPath = process.argv[2], outPath = process.argv[3];
const TW = parseInt(process.argv[4]), TH = parseInt(process.argv[5]);
const buf = fs.readFileSync(inPath);

if (buf.readUInt32BE(0) !== 0x89504E47) { console.error('not a PNG'); process.exit(1); }

// ---- parse chunks ----
let p = 8, width = 0, height = 0, bitDepth = 0, colorType = 0;
let idat = [];
let plte = null;
let trns = null;
while (p < buf.length) {
  const len = buf.readUInt32BE(p);
  const type = buf.toString('ascii', p + 4, p + 8);
  const data = buf.slice(p + 8, p + 8 + len);
  if (type === 'IHDR') {
    width = data.readUInt32BE(0); height = data.readUInt32BE(4);
    bitDepth = data[8]; colorType = data[9];
  } else if (type === 'PLTE') {
    plte = data;
  } else if (type === 'tRNS') {
    trns = data;
  } else if (type === 'IDAT') {
    idat.push(data);
  } else if (type === 'IEND') break;
  p += 12 + len;
}
if (bitDepth !== 8) { console.error('only 8-bit PNGs supported, got', bitDepth); process.exit(1); }

const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colorType];
const raw = zlib.inflateSync(Buffer.concat(idat));

// ---- unfilter scanlines -> RGBA ----
const bpp = channels;            // bytes per pixel (filtering unit)
const stride = width * bpp;
const out = Buffer.alloc(height * stride);
function paeth(a, b, c) {
  const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
  return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
let q = 0;
for (let y = 0; y < height; y++) {
  const filter = raw[q++];
  for (let x = 0; x < stride; x++) {
    const v = raw[q++];
    const a = x >= bpp ? out[y * stride + x - bpp] : 0;
    const b = y > 0 ? out[(y - 1) * stride + x] : 0;
    const c = (x >= bpp && y > 0) ? out[(y - 1) * stride + x - bpp] : 0;
    let r;
    switch (filter) {
      case 0: r = v; break;
      case 1: r = v + a; break;
      case 2: r = v + b; break;
      case 3: r = v + ((a + b) >> 1); break;
      case 4: r = v + paeth(a, b, c); break;
      default: r = v;
    }
    out[y * stride + x] = r & 0xff;
  }
}

// sample RGBA at source pixel (sx,sy); a=0 means transparent.
function sample(sx, sy) {
  const o = sy * stride + sx * bpp;
  if (colorType === 2) return [out[o], out[o+1], out[o+2], 255];
  if (colorType === 6) return [out[o], out[o+1], out[o+2], out[o+3]];
  if (colorType === 0) { const g = out[o]; return [g, g, g, 255]; }
  if (colorType === 4) { const g = out[o]; return [g, g, g, out[o+1]]; }
  if (colorType === 3) {
    const idx = out[o], i = idx * 3;
    const a = (trns && idx < trns.length) ? trns[idx] : 255;
    return [plte[i], plte[i+1], plte[i+2], a];
  }
  return [0, 0, 0, 255];
}

// ---- downscale (box average) to TW x TH, output BGR555 (0x0000 = transparent) ----
const px = Buffer.alloc(TW * TH * 2);
for (let ty = 0; ty < TH; ty++) {
  for (let tx = 0; tx < TW; tx++) {
    const sx0 = Math.floor(tx * width / TW), sx1 = Math.max(sx0 + 1, Math.floor((tx + 1) * width / TW));
    const sy0 = Math.floor(ty * height / TH), sy1 = Math.max(sy0 + 1, Math.floor((ty + 1) * height / TH));
    let r = 0, g = 0, b = 0, n = 0, op = 0;
    for (let sy = sy0; sy < sy1; sy++) for (let sx = sx0; sx < sx1; sx++) {
      const c = sample(sx, sy);
      if (c[3] < 128) continue;          // skip transparent samples
      r += c[0]; g += c[1]; b += c[2]; op++;
      n++;
    }
    let v;
    if (op === 0) {
      v = 0x0000;                        // fully transparent texel
    } else {
      r = (r/op)|0; g = (g/op)|0; b = (b/op)|0;
      v = ((b>>3)<<10) | ((g>>3)<<5) | (r>>3);
      if (v === 0) v = 0x0421;           // opaque but black -> nudge off 0
    }
    px.writeUInt16LE(v, (ty * TW + tx) * 2);
  }
}
fs.writeFileSync(outPath, px);
console.error(`${outPath}: ${TW}x${TH} from ${width}x${height} (ct ${colorType})`);
