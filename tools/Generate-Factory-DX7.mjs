// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Reproduce the V15 VMEM bank from the immutable stock image, trimmed to 32
// distinct voices for Flash. No network or device access.
// --syx DIR also writes the untrimmed bank as four 32-voice DX7 SysEx files
// (A-D), loadable into the DX7 library to restore any original slot.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import '../sdk/WebUpdate.js';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pkg=fs.readFileSync(new URL('../sdk/firmware/FM-1_015.fwsc',import.meta.url));
assert.equal(sha(pkg),'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a');
const original=FM1Web.inspectPackage(new Uint8Array(pkg),true).factory;
assert.equal(sha(original),'ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25');
const syxAt=process.argv.indexOf('--syx');
if(syxAt>0){
 const dir=process.argv[syxAt+1];assert(dir,'--syx needs a directory');
 fs.mkdirSync(dir,{recursive:true});
 for(let b=0;b<4;b++){
  const bank=[...original.subarray(b*4096,b*4096+4096)];
  const sum=bank.reduce((a,x)=>a+x,0);
  fs.writeFileSync(`${dir}/FM1-V15-factory-${'ABCD'[b]}.syx`,Uint8Array.from([240,67,0,9,32,0,...bank,(-sum)&127,247]));
 }
}
// Keep every program number valid (projects, program change, library banks
// address slots) but store only 32 distinct voices. Slots 0-95 are 12 families
// x 8 variants (slot = 32*group + 4*variant + family); variants 3-8 reuse
// variant 1 or 2 of the same family. The other one-off sounds reuse one of the
// eight kept ones. A copy encodes as a single match of a few bytes.
const KEEP_MISC=[96,97,104,108,109,113,117,121];
const source=s=>{
 if(s<96){const v=(s%32)>>2;return v<2?s:s-4*(v-(v&1));}
 return KEEP_MISC.includes(s)?s:KEEP_MISC[(s-96)%8];
};
const raw=new Uint8Array(original.length);
for(let s=0;s<128;s++)raw.set(original.subarray(source(s)*128,source(s)*128+128),s*128);
// VMEM literals are seven-bit. A high bit denotes a 3..130-byte match,
// followed by a one/two-byte base-128 backward distance. No heap is needed.
const packed=[];
for(let i=0;i<raw.length;){
 let length=2,distance=0;
 for(let j=0;j<i;j++){
  let n=0;while(n<130&&i+n<raw.length&&raw[j+n]===raw[i+n])n++;
  if(n>length){length=n;distance=i-j;}
 }
 if(distance){packed.push(128+length-3,(distance&127)|(distance>=128?128:0));if(distance>=128)packed.push(distance>>7);i+=length;}
 else{assert(raw[i]<128);packed.push(raw[i++]);}
}
const decoded=[];
for(let i=0;i<packed.length;){const token=packed[i++];if(token<128)decoded.push(token);else{
 const n=token-128+3,lo=packed[i++],distance=(lo&127)|(lo>=128?packed[i++]<<7:0);
 assert(distance>0&&distance<=decoded.length);
 for(let k=0;k<n;k++)decoded.push(decoded[decoded.length-distance]);
}}
assert.deepEqual(Buffer.from(decoded),Buffer.from(raw));
const lines=[];for(let i=0;i<packed.length;i+=24)lines.push('    '+packed.slice(i,i+24).join(',')+',');
const content='/* SPDX-License-Identifier: Apache-2.0\n'+
 ' * M-VAVE original V15 factory DX7 bank, modified: trimmed to 32 distinct voices,\n'+
 ' * the other 96 slots reuse a kept voice (tools/Generate-Factory-DX7.mjs). The full\n'+
 ' * bank ships as four DX7 library .syx files. Lossless encoding of the result.\n'+
 ' * Regenerate: node tools/Generate-Factory-DX7.mjs [--syx DIR]\n'+
 ' * Original VMEM SHA-256: '+sha(original)+'\n'+
 ' * Trimmed VMEM SHA-256: '+sha(raw)+'\n'+
 ' * Source and license: THIRD_PARTY.md (FM-1_015.fwsc). */\n'+
 'static const uint8_t factory_packed[] = {\n'+lines.join('\n')+'\n};\n';
const target=new URL('../app/fm_factory.inc',import.meta.url);
if(process.argv.includes('--check'))assert.equal(fs.readFileSync(target,'utf8'),content);
else fs.writeFileSync(target,content);
console.log(`V15 factory bank verified, trimmed to 32 distinct voices in 128 slots: ${raw.length} VMEM bytes, ${packed.length} encoded bytes.`);
