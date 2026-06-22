// Binary glTF (.glb) -> PS1 mesh baker (no Blender needed).
// Usage:
//   node tools/bake_glb.js <input.glb> --stats
//   node tools/bake_glb.js <input.glb> <out.c> <out.h> <name> [targetHeight] [gridDecim]
// gridDecim: optional integer N. If set, vertices are welded onto an N^3 grid
// (vertex-clustering decimation) to cut triangle count for the PS1.
const fs = require('fs');
const path = require('path');

const file = process.argv[2];
const buf = fs.readFileSync(file);
if (buf.toString('binary', 0, 4) !== 'glTF') { console.error('not a GLB'); process.exit(1); }

// ---- split chunks ----
let p = 12, json = null, bin = null;
while (p < buf.length) {
  const len = buf.readUInt32LE(p); const type = buf.readUInt32LE(p + 4); p += 8;
  const data = buf.slice(p, p + len); p += len;
  if (type === 0x4E4F534A) json = JSON.parse(data.toString('utf8'));
  else if (type === 0x004E4942) bin = data;
}
if (!json) { console.error('no JSON chunk'); process.exit(1); }

const COMP = { 5120: [1, 'Int8'], 5121: [1, 'UInt8'], 5122: [2, 'Int16'],
               5123: [2, 'UInt16'], 5125: [4, 'UInt32'], 5126: [4, 'Float'] };
const NCOMP = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };

function readAccessor(idx) {
  const acc = json.accessors[idx];
  const bv = json.bufferViews[acc.bufferView];
  const [csize, ctype] = COMP[acc.componentType];
  const n = NCOMP[acc.type];
  const base = (bv.byteOffset || 0) + (acc.byteOffset || 0);
  const stride = bv.byteStride || (csize * n);
  const out = new Array(acc.count * n);
  const rd = (ctype === 'UInt8' || ctype === 'Int8') ? (ctype + '') : (ctype + 'LE');
  for (let i = 0; i < acc.count; i++)
    for (let c = 0; c < n; c++)
      out[i * n + c] = bin['read' + rd](base + i * stride + c * csize);
  return out;
}

// ---- node transforms (4x4, column-major like glTF) ----
function matMul(a, b) {
  const o = new Array(16);
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) {
    let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
    o[c * 4 + r] = s;
  }
  return o;
}
function ident() { return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]; }
function fromTRS(t, q, s) {
  t = t || [0,0,0]; q = q || [0,0,0,1]; s = s || [1,1,1];
  const [x,y,z,w] = q;
  const xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
  const r = [
    (1-2*(yy+zz))*s[0], (2*(xy+wz))*s[0], (2*(xz-wy))*s[0], 0,
    (2*(xy-wz))*s[1], (1-2*(xx+zz))*s[1], (2*(yz+wx))*s[1], 0,
    (2*(xz+wy))*s[2], (2*(yz-wx))*s[2], (1-2*(xx+yy))*s[2], 0,
    t[0], t[1], t[2], 1,
  ];
  return r;
}
function nodeMatrix(nd) {
  if (nd.matrix) return nd.matrix.slice();
  return fromTRS(nd.translation, nd.rotation, nd.scale);
}
function xform(m, x, y, z) {
  return [ m[0]*x+m[4]*y+m[8]*z+m[12],
           m[1]*x+m[5]*y+m[9]*z+m[13],
           m[2]*x+m[6]*y+m[10]*z+m[14] ];
}

// ---- gather all primitives, transformed to world ----
const verts = [];   // [x,y,z]
const tris = [];    // [i,j,k]
function addMesh(meshIdx, m) {
  const mesh = json.meshes[meshIdx];
  for (const prim of mesh.primitives) {
    if (prim.attributes.POSITION === undefined) continue;
    const pos = readAccessor(prim.attributes.POSITION);
    const baseV = verts.length;
    for (let i = 0; i < pos.length; i += 3) {
      const w = xform(m, pos[i], pos[i+1], pos[i+2]);
      verts.push(w);
    }
    let idx;
    if (prim.indices !== undefined) idx = readAccessor(prim.indices);
    else { idx = []; for (let i = 0; i < pos.length/3; i++) idx.push(i); }
    for (let i = 0; i + 2 < idx.length; i += 3)
      tris.push([baseV + idx[i], baseV + idx[i+1], baseV + idx[i+2]]);
  }
}
function walk(nodeIdx, parent) {
  const nd = json.nodes[nodeIdx];
  const m = matMul(parent, nodeMatrix(nd));
  if (nd.mesh !== undefined) addMesh(nd.mesh, m);
  if (nd.children) for (const c of nd.children) walk(c, m);
}
const scene = json.scenes[json.scene || 0];
for (const n of scene.nodes) walk(n, ident());

// ---- bounds ----
let mn=[1e9,1e9,1e9], mx=[-1e9,-1e9,-1e9];
for (const v of verts) for (let a=0;a<3;a++){ if(v[a]<mn[a])mn[a]=v[a]; if(v[a]>mx[a])mx[a]=v[a]; }
const ext=[mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2]];
console.error(`glb: verts=${verts.length} tris=${tris.length}`);
console.error(`bbox X[${mn[0].toFixed(2)},${mx[0].toFixed(2)}] Y[${mn[1].toFixed(2)},${mx[1].toFixed(2)}] Z[${mn[2].toFixed(2)},${mx[2].toFixed(2)}]`);

if (process.argv[3] === '--stats' || process.argv.length < 6) process.exit(0);

// ---- map to PS1 world space ----
const outC = process.argv[3], outH = process.argv[4], name = process.argv[5];
const targetH = process.argv[6] ? parseFloat(process.argv[6]) : 300;
const grid = process.argv[7] ? parseInt(process.argv[7]) : 0;

let up=0; if(ext[1]>ext[up])up=1; if(ext[2]>ext[up])up=2;
const g=[0,1,2].filter(a=>a!==up); const g0=g[0],g1=g[1];
const height=Math.max(ext[up],1e-6); const scale=targetH/height;
const g0mid=(mn[g0]+mx[g0])/2, g1mid=(mn[g1]+mx[g1])/2;
console.error(`up-axis=${'XYZ'[up]} height=${height.toFixed(2)} scale=${scale.toFixed(4)}`);

let W = verts.map(c => [
  Math.round((c[g0]-g0mid)*scale),
  Math.round(-(c[up]-mn[up])*scale),
  Math.round((c[g1]-g1mid)*scale),
]);

let T = tris;

// ---- optional vertex-clustering decimation ----
if (grid > 0) {
  const cell = targetH / grid;
  const map = new Map(); const newV = []; const remap = new Array(W.length);
  for (let i=0;i<W.length;i++){
    const key = [Math.round(W[i][0]/cell),Math.round(W[i][1]/cell),Math.round(W[i][2]/cell)].join(',');
    if(map.has(key)) remap[i]=map.get(key);
    else { map.set(key,newV.length); remap[i]=newV.length; newV.push(W[i]); }
  }
  const seen=new Set(); const newT=[];
  for(const t of T){
    const a=remap[t[0]],b=remap[t[1]],c=remap[t[2]];
    if(a===b||b===c||a===c) continue; // degenerate
    const k=[a,b,c].sort((x,y)=>x-y).join(',');
    if(seen.has(k)) continue; seen.add(k);
    newT.push([a,b,c]);
  }
  console.error(`decimated: ${W.length}->${newV.length} verts, ${T.length}->${newT.length} tris`);
  W=newV; T=newT;
}

function faceColor(t){
  const a=W[t[0]],b=W[t[1]],c=W[t[2]];
  const ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2];
  const vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
  const nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
  const len=Math.hypot(nx,ny,nz)||1;
  const upf=Math.max(0,Math.min(1,(-ny/len+1)/2));
  const base=[160,150,140];
  return base.map(ch=>Math.min(255,Math.round(ch*(0.5+0.5*upf))));
}

let c=`#include "${path.basename(outH)}"\n\nstatic const SVECTOR _verts[] = {\n`;
for(const [x,y,z] of W) c+=`    { ${x}, ${y}, ${z}, 0 },\n`;
c+=`};\n\nstatic const unsigned short _faces[] = {\n`;
for(const t of T) c+=`    ${t[0]}, ${t[1]}, ${t[2]},\n`;
c+=`};\n\nstatic const unsigned char _colors[] = {\n`;
for(const t of T){const col=faceColor(t); c+=`    ${col[0]}, ${col[1]}, ${col[2]},\n`;}
c+=`};\n\nconst Mesh ${name} = {\n    _verts, ${W.length},\n    _faces, ${T.length},\n    _colors\n};\n`;

const guard=name.toUpperCase()+'_H';
fs.writeFileSync(outH,`#ifndef ${guard}\n#define ${guard}\n\n#include "engine3d.h"\n\nextern const Mesh ${name};\n\n#endif\n`);
fs.writeFileSync(outC,c);
console.error(`BAKED ${name}: ${W.length} verts, ${T.length} tris`);
