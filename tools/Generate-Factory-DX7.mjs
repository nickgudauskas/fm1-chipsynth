// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Build the factory DX7 bank: 11 chipsynth-style voices, then 32 voices of the
// V15 bank reproduced from the immutable stock image. No network or device access.
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
// DX7 imitations of the chipsynth NES presets (app/chip_presets.c order,
// without the wave bass).
// Names fit the 7 characters the track overview shows.
// FM can't switch duty or arpeggiate, so these approximate: a 2:1 modulator
// gives odd harmonics (square), 1:1 a buzzier pulse, feedback at high ratios
// noise, and the pitch EG (1 step = 1/32 octave around 50) the zap and coin.
// op: r/l = EG rates/levels, out = output level, c/f = coarse/fine ratio,
// d = detune (7 = none), v = key velocity sensitivity. ops[0] is OP1.
const op=(o={})=>({r:[99,50,50,60],l:[99,99,99,0],out:0,c:1,f:0,d:7,v:0,...o});
const chipVoice=({name,alg=5,fb=0,ops,lfo=[35,0,0,0,0,0],pr=[99,99,99,99],pl=[50,50,50,50]})=>{
 const b=new Uint8Array(128);
 for(let i=0;i<6;i++){const o=op(ops[5-i]),q=i*17;   // VMEM stores OP6 first
  b.set(o.r,q);b.set(o.l,q+4);b[q+8]=39;
  b[q+12]=o.d<<3;b[q+13]=o.v<<2;b[q+14]=o.out;b[q+15]=o.c<<1;b[q+16]=o.f;}
 b.set(pr,102);b.set(pl,106);b[110]=alg-1;b[111]=fb|8;
 const [speed,delay,pmd,amd,wave,pms]=lfo;
 b.set([speed,delay,pmd,amd,(wave<<1)|(pms<<4),24],112);
 b.set(Buffer.from(name.padEnd(10).slice(0,10),'ascii'),118);
 return b;
};
// Algorithm 5: three pairs OP2->OP1, OP4->OP3, OP6->OP5 (feedback on OP6).
// One pair alone sounds about 9 dB below the V15 voices, so single-tone
// voices repeat the same pair three times (identical phases, so no chorus).
const x3=(carrier,mod)=>[carrier,mod,carrier,mod,carrier,mod];
const lead={r:[99,60,50,62],l:[99,96,96,0],out:99,v:2};
const square={out:77,c:2};
const noise={out:99,c:31,f:41,r:[99,70,50,75],l:[99,0,0,0]};
const CHIP=[
 chipVoice({name:'SQ LEAD',lfo:[55,20,10,0,0,2],
  ops:x3(lead,{out:76,r:[99,60,50,62],l:[99,96,96,0]})}),
 chipVoice({name:'PWM LD',lfo:[50,30,8,0,0,2],   // detuned pairs beat like PWM
  ops:[{...lead,d:4},{out:76,d:4,l:[99,96,96,0]},{...lead,d:10},{out:76,d:10,l:[99,96,96,0]},lead,{out:76,l:[99,96,96,0]}]}),
 chipVoice({name:'TRI BAS',
  ops:x3({out:99,v:2,r:[99,50,50,65],l:[99,99,99,0]},{out:58,c:2})}),
 chipVoice({name:'PLUCK',
  ops:x3({out:99,v:2,r:[99,55,50,75],l:[99,0,0,0]},{...square,r:[99,55,50,75],l:[99,40,40,0]})}),
 chipVoice({name:'CHORD',   // root, major third, fifth as three squares
  ops:[{...lead,out:96},{out:76,c:2},{...lead,out:95,f:26},{out:76,c:2,f:26},{...lead,out:95,f:50},{out:76,c:3}]}),
 chipVoice({name:'ZAP',pr:[99,99,99,99],pl:[93,50,50,50],
  ops:x3({out:99,v:2,r:[99,60,50,75],l:[99,0,0,0]},square)}),
 chipVoice({name:'COIN',pr:[10,99,99,99],pl:[49,63,63,50],
  ops:x3({out:99,v:2,r:[99,45,50,70],l:[99,0,0,0]},square)}),
 chipVoice({name:'HAT',fb:7,
  ops:[{out:99,c:17,f:30,v:2,r:[99,72,50,80],l:[99,0,0,0]},{...noise,c:23},{},{},
   {out:99,c:13,f:50,v:2,r:[99,72,50,80],l:[99,0,0,0]},noise]}),
 chipVoice({name:'SNARE',fb:7,
  ops:[{out:96,v:2,r:[99,65,50,75],l:[99,0,0,0]},{out:60,c:2},{},{},
   {out:99,c:9,f:20,v:2,r:[99,60,50,75],l:[99,0,0,0]},noise]}),
 chipVoice({name:'METAL',fb:7,
  ops:[{},{},{out:95,f:41,v:2,r:[99,50,50,65],l:[99,70,70,0]},{out:80,c:5,f:19},
   {out:99,c:3,f:17,v:2,r:[99,50,50,65],l:[99,70,70,0]},{...noise,r:[99,50,50,65],l:[99,70,70,0]}]}),
];
// Sega Genesis YM2612-style voices. The YM2612 is 4-operator FM; its
// algorithms are DX7 algorithms with two operators silenced, so these use
// two or three pairs of algorithm 5 (a third pair for loudness) (or algorithm 32 for the organ),
// with feedback on OP6 like the YM2612's feedback on its first operator.
const pluckEnv={r:[99,55,40,70],l:[99,85,80,0]};
const GENESIS=[
 chipVoice({name:'GN SLAP',   // slap bass: bright 1:1 attack that closes fast, plus a click
  ops:[{out:99,v:2,...pluckEnv},{out:82,r:[99,70,50,70],l:[99,60,60,0]},
   {out:99,d:9,v:2,...pluckEnv},{out:70,c:7,r:[99,85,50,80],l:[99,0,0,0]},
   {out:99,d:5,v:2,...pluckEnv},{out:82,d:5,r:[99,70,50,70],l:[99,60,60,0]}]}),
 chipVoice({name:'GN BASS',fb:6,   // saw-like feedback bass over a sine sub
  ops:[{out:96,c:0,v:2,r:[99,50,50,70],l:[99,92,90,0]},{},
   {out:99,d:9,v:2,r:[99,50,50,70],l:[99,92,90,0]},{out:85,d:9,r:[99,60,50,70],l:[99,80,75,0]},
   {out:99,v:2,r:[99,50,50,70],l:[99,92,90,0]},{out:85,r:[99,60,50,70],l:[99,80,75,0]}]}),
 chipVoice({name:'GN LEAD',fb:7,lfo:[60,30,8,0,0,3],   // feedback saw plus a detuned square
  ops:[{out:99,d:9,v:2,r:[99,60,50,62],l:[99,96,96,0]},{out:76,c:2,d:9},
   {out:99,d:5,v:2,r:[99,60,50,62],l:[99,96,96,0]},{out:76,c:2,d:5},
   {out:99,v:2,r:[99,60,50,62],l:[99,96,96,0]},{out:80}]}),
 chipVoice({name:'GN BRAS',lfo:[45,40,6,0,0,2],   // modulators swell in for the brass bite
  ops:[{out:98,v:2,r:[70,50,50,65],l:[99,95,92,0]},{out:82,r:[55,50,50,70],l:[99,85,80,0]},
   {out:98,d:9,v:2,r:[70,50,50,65],l:[99,95,92,0]},{out:82,d:9,r:[55,50,50,70],l:[99,85,80,0]},
   {out:96,d:5,v:2,r:[70,50,50,65],l:[99,95,92,0]},{out:80,d:5,r:[55,50,50,70],l:[99,85,80,0]}]}),
 chipVoice({name:'GN EPNO',   // FM electric piano: a ratio-14 tine over a 1:1 body
  ops:[{out:99,v:3,r:[99,35,30,60],l:[99,80,0,0]},{out:68,c:14,r:[99,75,50,75],l:[99,0,0,0]},
   {out:99,d:8,v:3,r:[99,35,30,60],l:[99,80,0,0]},{out:72,r:[99,45,40,60],l:[99,60,0,0]},
   {out:99,d:6,v:3,r:[99,35,30,60],l:[99,80,0,0]},{out:72,d:6,r:[99,45,40,60],l:[99,60,0,0]}]}),
 chipVoice({name:'GN BELL',   // inharmonic 3.5 and 5.4 ratios ring like a bell
  ops:[{out:99,v:2,r:[99,30,25,45],l:[99,70,0,0]},{out:80,c:3,f:50,r:[99,40,30,50],l:[99,60,0,0]},
   {out:97,c:2,v:2,r:[99,30,25,45],l:[99,70,0,0]},{out:70,c:5,f:40,r:[99,40,30,50],l:[99,60,0,0]},
   {out:99,d:9,v:2,r:[99,30,25,45],l:[99,70,0,0]},{out:78,c:3,f:50,d:9,r:[99,40,30,50],l:[99,60,0,0]}]}),
 chipVoice({name:'GN ORCH',fb:7,pr:[99,85,99,99],pl:[54,50,50,50],   // orchestra hit: root, fifth, octave
  ops:[{out:98,v:2,r:[99,55,50,70],l:[99,0,0,0]},{out:85},{out:96,f:50,v:2,r:[99,55,50,70],l:[99,0,0,0]},
   {out:85,f:50},{out:96,c:2,v:2,r:[99,55,50,70],l:[99,0,0,0]},{out:80}]}),
 chipVoice({name:'GN KICK',pr:[99,92,99,99],pl:[82,50,50,50],   // one-octave pitch drop
  ops:x3({out:99,v:2,r:[99,60,50,70],l:[99,0,0,0]},{out:65,r:[99,85,50,80],l:[99,0,0,0]})}),
 chipVoice({name:'GN SNAR',fb:7,pr:[99,92,99,99],pl:[70,50,50,50],
  ops:[{out:97,v:2,r:[99,65,50,75],l:[99,0,0,0]},{out:62,c:2},{out:95,c:2,f:30,v:2,r:[99,70,50,75],l:[99,0,0,0]},{},
   {out:99,c:9,f:20,v:2,r:[99,58,50,75],l:[99,0,0,0]},noise]}),
 chipVoice({name:'GN TOM',pr:[99,75,99,99],pl:[70,50,50,50],
  ops:x3({out:99,v:2,r:[99,50,50,65],l:[99,0,0,0]},{out:60,r:[99,80,50,80],l:[99,0,0,0]})}),
 chipVoice({name:'GN STRG',lfo:[40,50,6,0,0,2],   // three detuned pairs with a slow attack
  ops:[{out:96,d:4,v:1,r:[45,50,50,55],l:[99,95,95,0]},{out:75,d:4},{out:96,d:10,v:1,r:[45,50,50,55],l:[99,95,95,0]},
   {out:75,d:10},{out:96,v:1,r:[45,50,50,55],l:[99,95,95,0]},{out:75}]}),
 chipVoice({name:'GN ORGN',alg:32,   // additive drawbars: 16', 8', 4', 2 2/3', 2'
  ops:[{out:93,c:0,r:[99,99,99,80],l:[99,99,99,0]},{out:96,r:[99,99,99,80],l:[99,99,99,0]},
   {out:95,c:2,r:[99,99,99,80],l:[99,99,99,0]},{out:90,c:3,r:[99,99,99,80],l:[99,99,99,0]},
   {out:84,c:4,r:[99,99,99,80],l:[99,99,99,0]},{out:88,d:9,r:[99,99,99,80],l:[99,99,99,0]}]}),
];
// Store only 54 distinct voices but keep 128 valid program numbers: slots
// 0-9 the NES-style chip voices, 10-21 the Genesis-style voices, 22-53 the
// kept V15 voices (variants 1-2 of the 12 families in original slot order,
// then 8 one-off sounds), then that sequence repeats. A repeat encodes as a
// single match of a few bytes.
const KEEP_MISC=[96,97,104,108,109,113,117,121];
const kept=[...Array(96).keys()].filter(s=>((s%32)>>2)<2).concat(KEEP_MISC);
const voices=[...CHIP,...GENESIS,...kept.map(s=>original.subarray(s*128,s*128+128))];
assert.equal(voices.length,54);
const raw=new Uint8Array(original.length);
for(let s=0;s<128;s++)raw.set(voices[s%voices.length],s*128);
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
const content='/* SPDX-License-Identifier: Apache-2.0 AND GPL-3.0-only WITH GPL-3.0-linking-exception\n'+
 ' * Slots 0-9: NES-style and 10-21: Sega Genesis-style voices written for FM1\n'+
 ' * Open (GPL-3.0-only, see tools/Generate-Factory-DX7.mjs). Slots 22-53: 32\n'+
 ' * voices of the M-VAVE V15 factory DX7 bank, modified by selection and\n'+
 ' * reordering; slots 54-127 repeat 0-53. The full V15 bank ships as four DX7\n'+
 ' * library .syx files.\n'+
 ' * Regenerate: node tools/Generate-Factory-DX7.mjs [--syx DIR]\n'+
 ' * Original VMEM SHA-256: '+sha(original)+'\n'+
 ' * Bank VMEM SHA-256: '+sha(raw)+'\n'+
 ' * Source and license: THIRD_PARTY.md (FM-1_015.fwsc). */\n'+
 'static const uint8_t factory_packed[] = {\n'+lines.join('\n')+'\n};\n';
const target=new URL('../app/fm_factory.inc',import.meta.url);
if(process.argv.includes('--check'))assert.equal(fs.readFileSync(target,'utf8'),content);
else fs.writeFileSync(target,content);
console.log(`Factory bank: 10 NES + 12 Genesis + 32 V15 voices in 128 slots: ${raw.length} VMEM bytes, ${packed.length} encoded bytes.`);
