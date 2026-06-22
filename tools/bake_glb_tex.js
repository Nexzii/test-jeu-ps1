// GLB -> textured PS1 mesh (POLY_FT3 with UVs) + extract embedded texture PNG.
// Usage: node tools/bake_glb_tex.js <in.glb> <out.c> <out.h> <name> <texPng> [targetH] [grid]
// Emits a TexMesh C file and writes the embedded texture to <texPng>
// (run tools/png2bin.js on that PNG afterwards).
const fs = require('fs');
const path = require('path');

const file = process.argv[2], outC = process.argv[3], outH = process.argv[4];
const name = process.argv[5], texPng = process.argv[6];
const targetH = process.argv[7] ? parseFloat(process.argv[7]) : 300;
const grid = process.argv[8] ? parseInt(process.argv[8]) : 0;

const buf = fs.readFileSync(file);
let p = 12, json = null, bin = null;
while (p < buf.length) {
  const len = buf.readUInt32LE(p), type = buf.readUInt32LE(p + 4); p += 8;
  const d = buf.slice(p, p + len); p += len;
  if (type === 0x4E4F534A) json = JSON.parse(d.toString('utf8'));
  else if (type === 0x004E4942) bin = d;
}

const COMP = { 5120:[1,'Int8'],5121:[1,'UInt8'],5122:[2,'Int16'],5123:[2,'UInt16'],5125:[4,'UInt32'],5126:[4,'Float'] };
const NCOMP = { SCALAR:1, VEC2:2, VEC3:3, VEC4:4, MAT4:16 };
function readAccessor(idx){
  const acc=json.accessors[idx], bv=json.bufferViews[acc.bufferView];
  const [cs,ct]=COMP[acc.componentType], n=NCOMP[acc.type];
  const base=(bv.byteOffset||0)+(acc.byteOffset||0), stride=bv.byteStride||(cs*n);
  const rd=(ct==='UInt8'||ct==='Int8')?ct:(ct+'LE');
  const out=new Array(acc.count*n);
  for(let i=0;i<acc.count;i++)for(let c=0;c<n;c++)out[i*n+c]=bin['read'+rd](base+i*stride+c*cs);
  return out;
}
function matMul(a,b){const o=new Array(16);for(let r=0;r<4;r++)for(let c=0;c<4;c++){let s=0;for(let k=0;k<4;k++)s+=a[k*4+r]*b[c*4+k];o[c*4+r]=s;}return o;}
function ident(){return [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];}
function fromTRS(t,q,s){t=t||[0,0,0];q=q||[0,0,0,1];s=s||[1,1,1];const[x,y,z,w]=q;const xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
  return [(1-2*(yy+zz))*s[0],(2*(xy+wz))*s[0],(2*(xz-wy))*s[0],0,(2*(xy-wz))*s[1],(1-2*(xx+zz))*s[1],(2*(yz+wx))*s[1],0,(2*(xz+wy))*s[2],(2*(yz-wx))*s[2],(1-2*(xx+yy))*s[2],0,t[0],t[1],t[2],1];}
function nodeMatrix(nd){return nd.matrix?nd.matrix.slice():fromTRS(nd.translation,nd.rotation,nd.scale);}
function xform(m,x,y,z){return [m[0]*x+m[4]*y+m[8]*z+m[12],m[1]*x+m[5]*y+m[9]*z+m[13],m[2]*x+m[6]*y+m[10]*z+m[14]];}

const verts=[], uvs=[], tris=[];
function addMesh(mi,m){
  for(const prim of json.meshes[mi].primitives){
    if(prim.attributes.POSITION===undefined)continue;
    const pos=readAccessor(prim.attributes.POSITION);
    const uv = prim.attributes.TEXCOORD_0!==undefined ? readAccessor(prim.attributes.TEXCOORD_0) : null;
    const base=verts.length;
    for(let i=0;i<pos.length;i+=3){const w=xform(m,pos[i],pos[i+1],pos[i+2]); verts.push(w);
      const ui=(i/3)*2; uvs.push(uv?[uv[ui],uv[ui+1]]:[0,0]); }
    let idx; if(prim.indices!==undefined)idx=readAccessor(prim.indices); else {idx=[];for(let i=0;i<pos.length/3;i++)idx.push(i);}
    for(let i=0;i+2<idx.length;i+=3)tris.push([base+idx[i],base+idx[i+1],base+idx[i+2]]);
  }
}
function walk(ni,par){const nd=json.nodes[ni];const m=matMul(par,nodeMatrix(nd));if(nd.mesh!==undefined)addMesh(nd.mesh,m);if(nd.children)for(const c of nd.children)walk(c,m);}
for(const n of json.scenes[json.scene||0].nodes) walk(n,ident());

// bounds + map to world (-Y up, feet at 0)
let mn=[1e9,1e9,1e9],mx=[-1e9,-1e9,-1e9];
for(const v of verts)for(let a=0;a<3;a++){if(v[a]<mn[a])mn[a]=v[a];if(v[a]>mx[a])mx[a]=v[a];}
const ext=[mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2]];
let up=0;if(ext[1]>ext[up])up=1;if(ext[2]>ext[up])up=2;
const g=[0,1,2].filter(a=>a!==up),g0=g[0],g1=g[1];
const scale=targetH/Math.max(ext[up],1e-6);
const g0m=(mn[g0]+mx[g0])/2,g1m=(mn[g1]+mx[g1])/2;
let W=verts.map(c=>[Math.round((c[g0]-g0m)*scale),Math.round(-(c[up]-mn[up])*scale),Math.round((c[g1]-g1m)*scale)]);
// UV -> 0..255 (V flipped: glTF V is bottom-up vs texture top-down? keep as-is, flip if needed)
let UV=uvs.map(t=>{let u=Math.round(t[0]*255),v=Math.round(t[1]*255);u=u<0?0:u>255?255:u;v=v<0?0:v>255?255:v;return [u,v];});
let T=tris;

if(grid>0){
  const cell=targetH/grid; const map=new Map(),nV=[],nUV=[],remap=new Array(W.length);
  for(let i=0;i<W.length;i++){const k=[Math.round(W[i][0]/cell),Math.round(W[i][1]/cell),Math.round(W[i][2]/cell)].join(',');
    if(map.has(k))remap[i]=map.get(k);else{map.set(k,nV.length);remap[i]=nV.length;nV.push(W[i]);nUV.push(UV[i]);}}
  const seen=new Set(),nT=[];
  for(const t of T){const a=remap[t[0]],b=remap[t[1]],c=remap[t[2]];if(a===b||b===c||a===c)continue;const kk=[a,b,c].slice().sort((x,y)=>x-y).join(',');if(seen.has(kk))continue;seen.add(kk);nT.push([a,b,c]);}
  console.error(`decimated ${W.length}->${nV.length} v, ${T.length}->${nT.length} t`); W=nV;UV=nUV;T=nT;
}

// emit TexMesh
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

// extract embedded texture PNG
if(json.images&&json.images[0]){
  const img=json.images[0];
  if(img.bufferView!==undefined){const bv=json.bufferViews[img.bufferView];fs.writeFileSync(texPng,bin.slice(bv.byteOffset||0,(bv.byteOffset||0)+bv.byteLength));console.error('texture ->',texPng);}
}
console.error(`BAKED ${name}: ${W.length} v, ${T.length} t, up=${'XYZ'[up]}`);
