// WAV (PCM) -> PS1 ADPCM (VAG body) for looping SPU playback.
// Usage: node tools/wav2adpcm.js <in.wav> <out.bin> <startSec> <durSec> <outRate>
const fs = require('fs');

const inPath = process.argv[2], outPath = process.argv[3];
const startSec = parseFloat(process.argv[4] || '1');
const durSec   = parseFloat(process.argv[5] || '4');
const outRate  = parseInt(process.argv[6] || '11025');
const b = fs.readFileSync(inPath);

// ---- parse WAV ----
let fmt = null, dataOff = 0, dataLen = 0, p = 12;
while (p < b.length - 8) {
  const id = b.toString('ascii', p, p + 4), sz = b.readUInt32LE(p + 4);
  if (id === 'fmt ') fmt = { ch: b.readUInt16LE(p + 10), rate: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22) };
  else if (id === 'data') { dataOff = p + 8; dataLen = sz; break; }
  p += 8 + sz + (sz & 1);
}
const { ch, rate, bits } = fmt;
const bytesPer = bits / 8;
const frame = bytesPer * ch;
const totalFrames = Math.floor(dataLen / frame);

function readSample(frameIdx, c) {       // -> 16-bit signed
  const o = dataOff + frameIdx * frame + c * bytesPer;
  if (bits === 24) { let v = b[o] | (b[o+1]<<8) | (b[o+2]<<16); if (v & 0x800000) v -= 0x1000000; return v >> 8; }
  if (bits === 16) return b.readInt16LE(o);
  if (bits === 8)  return (b[o] - 128) << 8;
  return 0;
}

// ---- extract mono segment, decimate to outRate ----
const step = Math.max(1, Math.round(rate / outRate));
const startFrame = Math.floor(startSec * rate);
const endFrame = Math.min(totalFrames, startFrame + Math.floor(durSec * rate));
const pcm = [];
for (let f = startFrame; f < endFrame; f += step) {
  let s = 0; for (let c = 0; c < ch; c++) s += readSample(f, c);
  pcm.push((s / ch) | 0);
}
// pad to multiple of 28
while (pcm.length % 28) pcm.push(0);

// ---- ADPCM encode ----
const F0 = [0, 60, 115, 98, 122];
const F1 = [0, 0, -52, -55, -60];
function clamp16(v){ return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

const nBlocks = pcm.length / 28;
const out = Buffer.alloc(nBlocks * 16);
let prev1 = 0, prev2 = 0;

for (let bi = 0; bi < nBlocks; bi++) {
  const blk = pcm.slice(bi * 28, bi * 28 + 28);
  let best = null;
  for (let filt = 0; filt < 5; filt++) {
    // find shift from max residual (using true prev as approximation)
    let maxd = 0, pp1 = prev1, pp2 = prev2;
    for (let i = 0; i < 28; i++) {
      const pred = (pp1 * F0[filt] + pp2 * F1[filt]) >> 6;
      const d = blk[i] - pred; const ad = Math.abs(d); if (ad > maxd) maxd = ad;
      pp2 = pp1; pp1 = blk[i];   // ideal prev
    }
    let shift = 0;
    while (shift < 12 && (7 << (12 - shift)) > maxd) shift++;   // largest step that fits, smallest shift such that step<=...
    // actually: smallest shift so that max quantized fits; refine:
    shift = 0;
    while (shift < 12 && maxd < (7 << (12 - (shift + 1)))) shift++;

    let err = 0, q1 = prev1, q2 = prev2;
    const nibs = new Array(28);
    for (let i = 0; i < 28; i++) {
      const pred = (q1 * F0[filt] + q2 * F1[filt]) >> 6;
      const diff = blk[i] - pred;
      const stepShift = 12 - shift;
      let q = Math.round(diff / (1 << stepShift));
      if (q > 7) q = 7; if (q < -8) q = -8;
      const recon = clamp16((q << stepShift) + pred);
      err += (recon - blk[i]) * (recon - blk[i]);
      q2 = q1; q1 = recon;
      nibs[i] = q & 0xF;
    }
    if (!best || err < best.err) best = { err, filt, shift, nibs, q1, q2 };
  }
  // commit chosen block
  prev1 = best.q1; prev2 = best.q2;
  const o = bi * 16;
  out[o] = (best.filt << 4) | best.shift;
  let flag = 0x00;
  if (bi === 0) flag = 0x04;                 // loop start
  if (bi === nBlocks - 1) flag = 0x03;       // loop end + repeat
  out[o + 1] = flag;
  for (let i = 0; i < 14; i++)
    out[o + 2 + i] = best.nibs[i*2] | (best.nibs[i*2+1] << 4);
}

fs.writeFileSync(outPath, out);
console.error(`${outPath}: ${nBlocks} blocks (${out.length} bytes), ${pcm.length} samples @ ${outRate}Hz`);
