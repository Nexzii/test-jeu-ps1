// OBJ + MTL (multi-texture) -> single TexMesh + a 256x256 texture ATLAS (BGR555).
// Packs each material's texture into a 4x4 grid of 64x64 cells and remaps UVs.
// Usage: node tools/bake_obj_atlas.js <model.obj> <out.c> <out.h> <name> <atlas.bin> [targetH]
const fs = require('fs');
const zlib = require('zlib');
const path = require('path');

const objPath = process.argv[2], outC = process.argv[3], outH = process.argv[4];
const name = process.argv[5], atlasPath = process.argv[6];
const targetH = process.argv[7] ? parseFloat(process.argv[7]) : 320;
const dir = path.dirname(objPath);

// ---- PNG decoder (8-bit; RGB/RGBA/palette+tRNS) -> {w,h,get(x,y)->[r,g,b]} ----
function loadPNG(file) {
  const b = fs.readFileSync(file);
  let p = 8, w, h, bd, ct, idat = [], plte = null, trns = null;
  while (p < b.length) {
    const len = b.readUInt32BE(p), t = b.toString('ascii', p+4, p+8), d = b.slice(p+8, p+8+len);
    if (t === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); bd = d[8]; ct = d[9]; }
    else if (t === 'PLTE') plte = d;
    else if (t === 'tRNS') trns = d;
    else if (t === 'IDAT') idat.push(d);
    else if (t === 'IEND') break;
    p += 12 + len;
  }
  const ch = { 0:1, 2:3, 3:1, 4:2, 6:4 }[ct];
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * ch, out = Buffer.alloc(h * stride);
  const paeth = (a,bb,c) => { const pp=a+bb-c,pa=Math.abs(pp-a),pb=Math.abs(pp-bb),pc=Math.abs(pp-c); return pa<=pb&&pa<=pc?a:pb<=pc?bb:c; };
  let q = 0;
  for (let y = 0; y < h; y++) { const f = raw[q++]; for (let x = 0; x < stride; x++) { const v = raw[q++];
    const a = x>=ch?out[y*stride+x-ch]:0, bb = y>0?out[(y-1)*stride+x]:0, c = (x>=ch&&y>0)?out[(y-1)*stride+x-ch]:0;
    let r; switch(f){case 1:r=v+a;break;case 2:r=v+bb;break;case 3:r=v+((a+bb)>>1);break;case 4:r=v+paeth(a,bb,c);break;default:r=v;}
    out[y*stride+x]=r&0xff; } }
  return { w, h, get(x,y){ const o=y*stride+x*ch;
    if(ct===2||ct===6)return[out[o],out[o+1],out[o+2]];
    if(ct===0||ct===4){const g=out[o];return[g,g,g];}
    if(ct===3){const i=out[o]*3;return[plte[i],plte[i+1],plte[i+2]];}
    return[0,0,0]; } };
}

// ---- parse MTL: material -> texture file ----
const mtlName = (fs.readFileSync(objPath,'utf8').match(/^mtllib\s+(.+)$/m)||[])[1];
const matTex = {};
if (mtlName) {
  const mtl = fs.readFileSync(path.join(dir, mtlName.trim()), 'utf8');
  let cur = null;
  for (const line of mtl.split(/\r?\n/)) {
    const m = line.match(/^newmtl\s+(.+)/); if (m) { cur = m[1].trim(); continue; }
    const k = line.match(/^\s*map_Kd\s+(.+)/i); if (k && cur) matTex[cur] = k[1].trim();
  }
}

// ---- build atlas (4x4 cells of 64x64) from unique textures ----
const CELL = 64, GRID = 4, ATLAS = CELL * GRID;
const uniqTex = [...new Set(Object.values(matTex))];
const cellOf = {};                         // texFile -> cell index
uniqTex.forEach((t, i) => cellOf[t] = i);
const atlas = Buffer.alloc(ATLAS * ATLAS * 2);
uniqTex.forEach((tex, i) => {
  const cx = (i % GRID) * CELL, cy = ((i / GRID) | 0) * CELL;
  let img; try { img = loadPNG(path.join(dir, tex)); } catch (e) { img = null; }
  for (let y = 0; y < CELL; y++) for (let x = 0; x < CELL; x++) {
    let r=128,g=128,bl=128;
    if (img) { const c = img.get((x*img.w/CELL)|0, (y*img.h/CELL)|0); r=c[0];g=c[1];bl=c[2]; }
    let v = ((bl>>3)<<10)|((g>>3)<<5)|(r>>3); if (v===0) v=0x0421;
    atlas.writeUInt16LE(v, ((cy+y)*ATLAS + (cx+x)) * 2);
  }
});
fs.writeFileSync(atlasPath, atlas);

// ---- parse OBJ ----
const V = [], VT = [];
const faces = [];                          // each: [{vi,ti}], mat
let curMat = null;
for (const line of fs.readFileSync(objPath,'utf8').split(/\r?\n/)) {
  const p = line.split(/\s+/);
  if (p[0] === 'v')  V.push([+p[1], +p[2], +p[3]]);
  else if (p[0] === 'vt') VT.push([+p[1], +p[2]]);
  else if (p[0] === 'usemtl') curMat = p[1];
  else if (p[0] === 'f') {
    const verts = p.slice(1).map(s => { const a = s.split('/'); return { vi:+a[0]-1, ti:a[1]?+a[1]-1:-1 }; });
    for (let k = 1; k + 1 < verts.length; k++) faces.push({ v:[verts[0],verts[k],verts[k+1]], mat:curMat });
  }
}

// ---- build TexMesh: split vertices by (vi,ti,mat), remap UV into atlas cell ----
const outV = [], outUV = [], outF = [];
const vmap = new Map();
function frac(x){ x -= Math.floor(x); return x; }
function addVert(vi, ti, mat) {
  const key = vi + '/' + ti + '/' + mat;
  if (vmap.has(key)) return vmap.get(key);
  const id = outV.length;
  outV.push(V[vi]);
  const cell = cellOf[matTex[mat]] ?? 0;
  const cx = (cell % GRID), cy = (cell / GRID) | 0;
  let u = 0.5, v = 0.5;
  if (ti >= 0 && VT[ti]) { u = frac(VT[ti][0]); v = frac(1 - VT[ti][1]); }
  const au = Math.round((cx + u) * CELL); const av = Math.round((cy + v) * CELL);
  outUV.push([Math.min(255, au), Math.min(255, av)]);
  vmap.set(key, id);
  return id;
}
for (const f of faces) {
  const a = addVert(f.v[0].vi, f.v[0].ti, f.mat);
  const b = addVert(f.v[1].vi, f.v[1].ti, f.mat);
  const c = addVert(f.v[2].vi, f.v[2].ti, f.mat);
  outF.push([a, b, c]);
}

// ---- map positions to world (auto up-axis, feet at 0) ----
let mn=[1e9,1e9,1e9], mx=[-1e9,-1e9,-1e9];
for (const v of outV) for (let a=0;a<3;a++){ if(v[a]<mn[a])mn[a]=v[a]; if(v[a]>mx[a])mx[a]=v[a]; }
const ext=[mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2]];
let up=0; if(ext[1]>ext[up])up=1; if(ext[2]>ext[up])up=2;
const g=[0,1,2].filter(a=>a!==up), g0=g[0], g1=g[1];
const scale=targetH/Math.max(ext[up],1e-6);
const g0m=(mn[g0]+mx[g0])/2, g1m=(mn[g1]+mx[g1])/2;
const W = outV.map(c => [Math.round((c[g0]-g0m)*scale), Math.round(-(c[up]-mn[up])*scale), Math.round((c[g1]-g1m)*scale)]);

let cc = `#include "${path.basename(outH)}"\n\nstatic const SVECTOR _v[]={\n`;
for (const [x,y,z] of W) cc += `{${x},${y},${z},0},`;
cc += `\n};\nstatic const unsigned short _f[]={\n`;
for (const t of outF) cc += `${t[0]},${t[1]},${t[2]},`;
cc += `\n};\nstatic const unsigned char _uv[]={\n`;
for (const [u,v] of outUV) cc += `${u},${v},`;
cc += `\n};\nconst TexMesh ${name}={_v,${W.length},_f,${outF.length},_uv};\n`;
const guard = name.toUpperCase()+'_H';
fs.writeFileSync(outH, `#ifndef ${guard}\n#define ${guard}\n#include "engine3d.h"\nextern const TexMesh ${name};\n#endif\n`);
fs.writeFileSync(outC, cc);
console.error(`BAKED ${name}: ${W.length}v ${outF.length}t, atlas ${uniqTex.length} tex -> ${atlasPath}, up=${'XYZ'[up]}`);
