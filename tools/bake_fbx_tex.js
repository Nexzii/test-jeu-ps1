// Binary FBX -> textured PS1 TexMesh (geometry + UVs). External texture (PNG)
// is converted separately with png2bin.js.
// Usage: node tools/bake_fbx_tex.js <in.fbx> <out.c> <out.h> <name> [targetH] [grid]
const fs = require('fs');
const zlib = require('zlib');

const file = process.argv[2], outC = process.argv[3], outH = process.argv[4];
const name = process.argv[5];
const targetH = process.argv[6] ? parseFloat(process.argv[6]) : 300;
const grid = process.argv[7] ? parseInt(process.argv[7]) : 0;
const buf = fs.readFileSync(file);

if (!buf.toString('binary', 0, 21).startsWith('Kaydara FBX Binary')) { console.error('not binary FBX'); process.exit(1); }
const version = buf.readUInt32LE(23), is64 = version >= 7500;
let off = 27;

function readNode(start) {
  let p = start;
  const endOffset = is64 ? Number(buf.readBigUInt64LE(p)) : buf.readUInt32LE(p); p += is64 ? 8 : 4;
  const numProps  = is64 ? Number(buf.readBigUInt64LE(p)) : buf.readUInt32LE(p); p += is64 ? 8 : 4;
  p += is64 ? 8 : 4;
  const nameLen = buf.readUInt8(p); p += 1;
  if (endOffset === 0) return { node: null, next: start + (is64 ? 25 : 13) };
  const nm = buf.toString('binary', p, p + nameLen); p += nameLen;
  const props = [];
  for (let i = 0; i < numProps; i++) {
    const t = String.fromCharCode(buf.readUInt8(p)); p += 1;
    if (t === 'Y') { props.push(buf.readInt16LE(p)); p += 2; }
    else if (t === 'C') { props.push(buf.readUInt8(p)); p += 1; }
    else if (t === 'I') { props.push(buf.readInt32LE(p)); p += 4; }
    else if (t === 'F') { props.push(buf.readFloatLE(p)); p += 4; }
    else if (t === 'D') { props.push(buf.readDoubleLE(p)); p += 8; }
    else if (t === 'L') { props.push(Number(buf.readBigInt64LE(p))); p += 8; }
    else if (t === 'S' || t === 'R') { const len = buf.readUInt32LE(p); p += 4; props.push(t === 'S' ? buf.toString('binary', p, p + len) : buf.slice(p, p + len)); p += len; }
    else if ('fdlib'.includes(t)) {
      const arrLen = buf.readUInt32LE(p); p += 4; const enc = buf.readUInt32LE(p); p += 4; const compLen = buf.readUInt32LE(p); p += 4;
      let data = buf.slice(p, p + compLen); p += compLen; if (enc === 1) data = zlib.inflateSync(data);
      const es = { f:4,d:8,l:8,i:4,b:1 }[t]; const arr = new Array(arrLen);
      for (let k = 0; k < arrLen; k++) { const o = k*es; arr[k] = t==='f'?data.readFloatLE(o):t==='d'?data.readDoubleLE(o):t==='i'?data.readInt32LE(o):t==='l'?Number(data.readBigInt64LE(o)):data.readUInt8(o); }
      props.push(arr);
    } else { console.error('bad prop', t); process.exit(1); }
  }
  const children = [];
  while (p < endOffset) { const r = readNode(p); if (r.node) children.push(r.node); p = r.next; if (!r.node) break; }
  return { node: { name: nm, props, children }, next: endOffset };
}
const root = [];
while (off < buf.length) { const r = readNode(off); if (!r.node) break; root.push(r.node); off = r.next; if (off + (is64?25:13) > buf.length) break; }
function findAll(nodes, n, out=[]) { for (const x of nodes) { if (x.name === n) out.push(x); if (x.children) findAll(x.children, n, out); } return out; }

const geos = findAll(root, 'Geometry');
let best=null,bv=-1; for (const g of geos){const v=findAll(g.children,'Vertices')[0]; if(v&&v.props[0]&&v.props[0].length>bv){bv=v.props[0].length;best=g;}}
const vertsFlat = findAll(best.children,'Vertices')[0].props[0];
const polyIdx   = findAll(best.children,'PolygonVertexIndex')[0].props[0];
const nVerts = vertsFlat.length/3;

// UV layer
const uvNode = findAll(best.children,'LayerElementUV')[0];
let UVraw=null, UVidx=null, uvMap='ByPolygonVertex', uvRef='IndexToDirect';
if (uvNode) {
  const u=findAll(uvNode.children,'UV')[0]; if(u)UVraw=u.props[0];
  const ui=findAll(uvNode.children,'UVIndex')[0]; if(ui)UVidx=ui.props[0];
  const mp=findAll(uvNode.children,'MappingInformationType')[0]; if(mp)uvMap=mp.props[0];
  const rf=findAll(uvNode.children,'ReferenceInformationType')[0]; if(rf)uvRef=rf.props[0];
}
console.error(`verts=${nVerts} uvMap=${uvMap} uvRef=${uvRef} uvCount=${UVraw?UVraw.length/2:0}`);

function cornerUV(pv, vIdx) {
  if (!UVraw) return [0,0];
  if (uvMap.indexOf('ByVertex')>=0 || uvMap.indexOf('ByControlPoint')>=0) {
    let i = (uvRef==='Direct') ? vIdx : (UVidx?UVidx[vIdx]:vIdx);
    return [UVraw[i*2], UVraw[i*2+1]];
  }
  // ByPolygonVertex
  let i = (uvRef==='Direct') ? pv : (UVidx?UVidx[pv]:pv);
  return [UVraw[i*2], UVraw[i*2+1]];
}

// triangulate, tracking polygon-vertex index for UVs
const tris=[]; const vertUV=new Array(nVerts).fill(null);
let poly=[];
for (let i=0;i<polyIdx.length;i++){
  let idx=polyIdx[i], end=false; if(idx<0){idx=~idx;end=true;}
  poly.push({v:idx, pv:i});
  if(end){
    for(const c of poly){ const uv=cornerUV(c.pv,c.v); if(!vertUV[c.v]) vertUV[c.v]=uv; }
    for(let k=1;k+1<poly.length;k++) tris.push([poly[0].v,poly[k].v,poly[k+1].v]);
    poly=[];
  }
}

// bounds + map (auto up-axis, feet at 0)
let mn=[1e9,1e9,1e9],mx=[-1e9,-1e9,-1e9];
for(let i=0;i<nVerts;i++)for(let a=0;a<3;a++){const c=vertsFlat[i*3+a];if(c<mn[a])mn[a]=c;if(c>mx[a])mx[a]=c;}
const ext=[mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2]];
let up=0;if(ext[1]>ext[up])up=1;if(ext[2]>ext[up])up=2;
const g=[0,1,2].filter(a=>a!==up),g0=g[0],g1=g[1];
const scale=targetH/Math.max(ext[up],1e-6);
const g0m=(mn[g0]+mx[g0])/2,g1m=(mn[g1]+mx[g1])/2;
let W=[]; for(let i=0;i<nVerts;i++){const c=[vertsFlat[i*3],vertsFlat[i*3+1],vertsFlat[i*3+2]];
  W.push([Math.round((c[g0]-g0m)*scale),Math.round(-(c[up]-mn[up])*scale),Math.round((c[g1]-g1m)*scale)]);}
let UV=vertUV.map(t=>{if(!t)return[0,0];let u=Math.round(t[0]*255),v=Math.round((1-t[1])*255);u=u<0?0:u>255?255:u;v=v<0?0:v>255?255:v;return[u,v];});
let T=tris;

if(grid>0){
  const cell=targetH/grid;const map=new Map(),nV=[],nUV=[],rm=new Array(W.length);
  for(let i=0;i<W.length;i++){const k=[Math.round(W[i][0]/cell),Math.round(W[i][1]/cell),Math.round(W[i][2]/cell)].join(',');if(map.has(k))rm[i]=map.get(k);else{map.set(k,nV.length);rm[i]=nV.length;nV.push(W[i]);nUV.push(UV[i]);}}
  const seen=new Set(),nT=[];for(const t of T){const a=rm[t[0]],b=rm[t[1]],c=rm[t[2]];if(a===b||b===c||a===c)continue;const kk=[a,b,c].slice().sort((x,y)=>x-y).join(',');if(seen.has(kk))continue;seen.add(kk);nT.push([a,b,c]);}
  console.error(`decimated ${W.length}->${nV.length}v ${T.length}->${nT.length}t`);W=nV;UV=nUV;T=nT;
}

const path=require('path');
let c=`#include "${path.basename(outH)}"\n\nstatic const SVECTOR _v[]={\n`;
for(const[x,y,z]of W)c+=`{${x},${y},${z},0},`;
c+=`\n};\nstatic const unsigned short _f[]={\n`;
for(const t of T)c+=`${t[0]},${t[1]},${t[2]},`;
c+=`\n};\nstatic const unsigned char _uv[]={\n`;
for(const[u,v]of UV)c+=`${u},${v},`;
c+=`\n};\nconst TexMesh ${name}={_v,${W.length},_f,${T.length},_uv};\n`;
const guard=name.toUpperCase()+'_H';
fs.writeFileSync(outH,`#ifndef ${guard}\n#define ${guard}\n#include "engine3d.h"\nextern const TexMesh ${name};\n#endif\n`);
fs.writeFileSync(outC,c);
console.error(`BAKED ${name}: ${W.length}v ${T.length}t up=${'XYZ'[up]}`);
