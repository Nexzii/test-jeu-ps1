// Minimal binary-FBX reader + PS1 mesh baker (no Blender needed).
// Usage:
//   node tools/bake_fbx.js <input.fbx> [--stats]
//   node tools/bake_fbx.js <input.fbx> <out.c> <out.h> <name> [targetHeight]
const fs = require('fs');
const zlib = require('zlib');

const file = process.argv[2];
const buf = fs.readFileSync(file);

// ---- header ----
const magic = buf.toString('binary', 0, 21);
if (!magic.startsWith('Kaydara FBX Binary')) { console.error('not a binary FBX'); process.exit(1); }
const version = buf.readUInt32LE(23);
const is64 = version >= 7500;
let off = 27;

function readNode(start) {
  let p = start;
  const endOffset = is64 ? Number(buf.readBigUInt64LE(p)) : buf.readUInt32LE(p); p += is64 ? 8 : 4;
  const numProps  = is64 ? Number(buf.readBigUInt64LE(p)) : buf.readUInt32LE(p); p += is64 ? 8 : 4;
  /* propListLen */ p += is64 ? 8 : 4;
  const nameLen = buf.readUInt8(p); p += 1;
  if (endOffset === 0) return { node: null, next: start + (is64 ? 25 : 13) };
  const name = buf.toString('binary', p, p + nameLen); p += nameLen;

  const props = [];
  for (let i = 0; i < numProps; i++) {
    const t = String.fromCharCode(buf.readUInt8(p)); p += 1;
    if (t === 'Y') { props.push(buf.readInt16LE(p)); p += 2; }
    else if (t === 'C') { props.push(buf.readUInt8(p) !== 0); p += 1; }
    else if (t === 'I') { props.push(buf.readInt32LE(p)); p += 4; }
    else if (t === 'F') { props.push(buf.readFloatLE(p)); p += 4; }
    else if (t === 'D') { props.push(buf.readDoubleLE(p)); p += 8; }
    else if (t === 'L') { props.push(Number(buf.readBigInt64LE(p))); p += 8; }
    else if (t === 'S' || t === 'R') {
      const len = buf.readUInt32LE(p); p += 4;
      props.push(t === 'S' ? buf.toString('binary', p, p + len) : buf.slice(p, p + len));
      p += len;
    } else if ('fdlib'.includes(t)) {
      const arrLen = buf.readUInt32LE(p); p += 4;
      const enc = buf.readUInt32LE(p); p += 4;
      const compLen = buf.readUInt32LE(p); p += 4;
      let data = buf.slice(p, p + compLen); p += compLen;
      if (enc === 1) data = zlib.inflateSync(data);
      const elemSize = { f: 4, d: 8, l: 8, i: 4, b: 1 }[t];
      const arr = new Array(arrLen);
      for (let k = 0; k < arrLen; k++) {
        const o = k * elemSize;
        if (t === 'f') arr[k] = data.readFloatLE(o);
        else if (t === 'd') arr[k] = data.readDoubleLE(o);
        else if (t === 'i') arr[k] = data.readInt32LE(o);
        else if (t === 'l') arr[k] = Number(data.readBigInt64LE(o));
        else arr[k] = data.readUInt8(o);
      }
      props.push(arr);
    } else { console.error('unknown prop type', t, 'at', p); process.exit(1); }
  }

  const children = [];
  while (p < endOffset) {
    const r = readNode(p);
    if (r.node) children.push(r.node);
    p = r.next;
    if (!r.node) break;
  }
  return { node: { name, props, children }, next: endOffset };
}

// parse top-level nodes
const root = [];
while (off < buf.length) {
  const r = readNode(off);
  if (!r.node) break;
  root.push(r.node);
  off = r.next;
  if (off + (is64 ? 25 : 13) > buf.length) break;
}

function findAll(nodes, name, out = []) {
  for (const n of nodes) {
    if (n.name === name) out.push(n);
    if (n.children) findAll(n.children, name, out);
  }
  return out;
}

const geos = findAll(root, 'Geometry');
console.error(`FBX v${version}  64bit=${is64}  Geometry nodes=${geos.length}`);

// pick the geometry with the most vertices
let best = null, bestV = -1;
for (const g of geos) {
  const v = findAll(g.children, 'Vertices')[0];
  if (v && v.props[0] && v.props[0].length > bestV) { bestV = v.props[0].length; best = g; }
}
if (!best) { console.error('no Vertices found'); process.exit(1); }

const vertsFlat = findAll(best.children, 'Vertices')[0].props[0];
const polyIdx   = findAll(best.children, 'PolygonVertexIndex')[0].props[0];
const nVerts = vertsFlat.length / 3;

// triangulate polygons (negative index = ~idx marks polygon end)
const tris = [];
let poly = [];
for (let i = 0; i < polyIdx.length; i++) {
  let idx = polyIdx[i];
  let end = false;
  if (idx < 0) { idx = ~idx; end = true; }
  poly.push(idx);
  if (end) {
    for (let k = 1; k + 1 < poly.length; k++) tris.push([poly[0], poly[k], poly[k + 1]]);
    poly = [];
  }
}

// bounds (FBX assumed Y-up)
let mnx=1e9,mny=1e9,mnz=1e9,mxx=-1e9,mxy=-1e9,mxz=-1e9;
for (let i=0;i<nVerts;i++){const x=vertsFlat[i*3],y=vertsFlat[i*3+1],z=vertsFlat[i*3+2];
  if(x<mnx)mnx=x;if(y<mny)mny=y;if(z<mnz)mnz=z;if(x>mxx)mxx=x;if(y>mxy)mxy=y;if(z>mxz)mxz=z;}
console.error(`verts=${nVerts}  tris=${tris.length}`);
console.error(`bbox X[${mnx.toFixed(2)},${mxx.toFixed(2)}] Y[${mny.toFixed(2)},${mxy.toFixed(2)}] Z[${mnz.toFixed(2)},${mxz.toFixed(2)}]`);

if (process.argv[3] === '--stats' || process.argv.length < 6) process.exit(0);

// ---- bake to C ----
const outC = process.argv[3], outH = process.argv[4], name = process.argv[5];
const targetH = process.argv[6] ? parseFloat(process.argv[6]) : 300;

// Detect the vertical axis as the one with the largest extent (a standing
// character is taller than it is wide/deep). 0=X,1=Y,2=Z.
const mn = [mnx, mny, mnz], mx = [mxx, mxy, mxz];
const ext = [mxx-mnx, mxy-mny, mxz-mnz];
let up = 0; if (ext[1] > ext[up]) up = 1; if (ext[2] > ext[up]) up = 2;
const ground = [0,1,2].filter(a => a !== up); // the two horizontal axes
const g0 = ground[0], g1 = ground[1];
const height = Math.max(ext[up], 1e-6);
const scale = targetH / height;
const g0mid = (mn[g0]+mx[g0])/2, g1mid = (mn[g1]+mx[g1])/2;
console.error(`up-axis=${'XYZ'[up]}  height=${height.toFixed(2)}  scale=${scale.toFixed(4)}`);

function W(i){ // -> world (-Y up, feet at y=0, centered on the ground plane)
  const c = [vertsFlat[i*3], vertsFlat[i*3+1], vertsFlat[i*3+2]];
  return [Math.round((c[g0] - g0mid) * scale),
          Math.round(-(c[up] - mn[up]) * scale),
          Math.round((c[g1] - g1mid) * scale)];
}
let V = []; for (let i=0;i<nVerts;i++) V.push(W(i));
let T = tris;

// optional vertex-clustering decimation (last CLI arg = grid resolution)
const grid = process.argv[7] ? parseInt(process.argv[7]) : 0;
if (grid > 0) {
  const cell = targetH / grid;
  const map = new Map(); const newV = []; const remap = new Array(V.length);
  for (let i=0;i<V.length;i++){
    const key=[Math.round(V[i][0]/cell),Math.round(V[i][1]/cell),Math.round(V[i][2]/cell)].join(',');
    if(map.has(key)) remap[i]=map.get(key);
    else { map.set(key,newV.length); remap[i]=newV.length; newV.push(V[i]); }
  }
  const seen=new Set(); const newT=[];
  for(const t of T){
    const a=remap[t[0]],b=remap[t[1]],c=remap[t[2]];
    if(a===b||b===c||a===c) continue;
    const k=[a,b,c].slice().sort((x,y)=>x-y).join(',');
    if(seen.has(k)) continue; seen.add(k);
    newT.push([a,b,c]);
  }
  console.error(`decimated: ${V.length}->${newV.length} verts, ${T.length}->${newT.length} tris`);
  V=newV; T=newT;
}

function faceColor(t){ // shade by face normal "up" (world -Y up)
  const a=V[t[0]],b=V[t[1]],c=V[t[2]];
  const ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2];
  const vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
  let ny=uz*vx-ux*vz; // y component of cross(u,v)
  const len=Math.hypot(uy*vz-uz*vy, ny, ux*vy-uy*vx)||1;
  const up=Math.max(0,Math.min(1,(-ny/len+1)/2)); // -Y is up
  const base=[150,150,165];
  return base.map(ch=>Math.min(255,Math.round(ch*(0.55+0.45*up))));
}

let c = `#include "${outH.split(/[\\/]/).pop()}"\n\n`;
c += `static const SVECTOR _verts[] = {\n`;
for (const [x,y,z] of V) c += `    { ${x}, ${y}, ${z}, 0 },\n`;
c += `};\n\nstatic const unsigned short _faces[] = {\n`;
for (const t of T) c += `    ${t[0]}, ${t[1]}, ${t[2]},\n`;
c += `};\n\nstatic const unsigned char _colors[] = {\n`;
for (const t of T){ const col=faceColor(t); c += `    ${col[0]}, ${col[1]}, ${col[2]},\n`; }
c += `};\n\nconst Mesh ${name} = {\n    _verts, ${V.length},\n    _faces, ${T.length},\n    _colors\n};\n`;

const guard = name.toUpperCase()+'_H';
fs.writeFileSync(outH, `#ifndef ${guard}\n#define ${guard}\n\n#include "engine3d.h"\n\nextern const Mesh ${name};\n\n#endif\n`);
fs.writeFileSync(outC, c);
console.error(`BAKED ${name}: ${V.length} verts, ${T.length} tris, scale ${scale.toFixed(4)}`);
