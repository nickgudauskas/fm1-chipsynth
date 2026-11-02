// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Build the NES and GENESIS project presets (.fm1) by driving the browser
// simulator core, so the files come from the same capture/export code as the
// panel. Needs build/web/simulator.wasm: run tools/Build-Web.ps1 -LocalPreview
// first. Usage: node chipsynth/presets/Make-Presets.mjs
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,'../..'), out=here;
await import(root+'/sdk/WebUpdate.js'); await import(root+'/sdk/Simulator.js');
const wasm=fs.readFileSync(root+'/build/web/simulator.wasm');
const CH=[0,1,2,3,4,5,6,9];   // default track MIDI channels: 1-7, track 8 on 10
const fresh=()=>new globalThis.FM1SimulatorCore(wasm);
const run=(core,msg)=>{const r=core.command(msg);return r;};
function build(name,programs){
  const core=fresh();
  for(let t=0;t<8;t++){
    run(core,{type:'select',track:t,source:0});                 // DX7 FM
    run(core,{type:'midi',data:Uint8Array.from([0xC0|CH[t],programs[t]])});
    for(let i=0;i<8;i++)core.c.sim_render();
  }
  let p=run(core,{type:'export'}); p=p?.data??p;
  const bytes=new Uint8Array(p);
  const nameAt=8*64*4+8*223+2+2;
  bytes.set(Buffer.from(name.padEnd(12).slice(0,12),'ascii'),nameAt);
  bytes[nameAt-2]=0;bytes[nameAt-1]=0;                         // no muted or soloed tracks
  // re-import into a clean core to prove the file loads, then export what it reads back
  const check=fresh(); run(check,{type:'import',data:bytes});
  let back=run(check,{type:'export'}); back=new Uint8Array(back?.data??back);
  const types=[],progs=[];const mute=back[nameAt-2],solo=back[nameAt-1];
  for(let t=0;t<8;t++){const at=2048+t*223;types.push(back[at]);progs.push(back[at+2]);}
  const file=FM1Web.projectFile(FM1Web.tunedProject(back));
  FM1Web.readProjectFile(file);
  fs.writeFileSync(`${out}/${name}.fm1`,file);
  console.log(name,'types',types.join(','),'programs',progs.join(','),'mute',mute,'solo',solo,'name',JSON.stringify(Buffer.from(back.subarray(nameAt,nameAt+12)).toString()),file.length,'bytes');
}
// Factory bank slots (tools/Generate-Factory-DX7.mjs), one per track.
build('NES',[0,1,2,7,8,4,6,9]);          // SQ LEAD PWM LD TRI BAS HAT SNARE CHORD COIN METAL
build('GENESIS',[12,13,10,17,18,14,20,16]); // GN LEAD BRAS SLAP KICK SNAR EPNO STRG ORCH
