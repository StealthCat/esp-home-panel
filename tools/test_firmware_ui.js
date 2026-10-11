#!/usr/bin/env node
// Exercise the shipped page's JavaScript with a tiny DOM/network fixture.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../main/firmware.html'), 'utf8');
const source = html.split('<script>')[1].split('</script>')[0];
const ready = {version:'test',build:'aaaa',pending:false,max_size:7340032,token:'before'};
async function fixture(states=[ready], upload={status:200, responseText:'{"build":"bbbb"}'}) {
  const nodes = Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1], {
    disabled:['file','upload'].includes(m[1]), files:[], events:{},
    addEventListener(name, fn) {this.events[name]=fn;}
  }]));
  let last=states[states.length-1], sent=null;
  class XHR {
    headers={};
    upload={};
    open(method, url) {assert.equal(method,'POST');assert.equal(url,'/firmware/upload');}
    setRequestHeader(key,value) {this.headers[key]=value;}
    send(file) {sent={file,headers:this.headers};this.upload.onprogress({lengthComputable:true,loaded:file.size,total:file.size});Object.assign(this,upload);this.onload();}
  }
  vm.runInNewContext(source, {
    document:{getElementById:id=>{assert.ok(nodes[id],`Unknown element ${id}`);return nodes[id];}},
    window:{addEventListener(){}}, confirm:()=>true, AbortSignal,
    setTimeout:fn=>{queueMicrotask(fn);}, XMLHttpRequest:XHR,
    fetch:async()=>{const value=states.length?states.shift():last;if(value.error)return {ok:false,status:value.error};return {ok:true,json:async()=>value};}
  });
  const settle=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
  await settle();
  return {nodes,settle,sent:()=>sent};
}
(async()=>{
  let f=await fixture();assert.equal(f.nodes.version.textContent,'test');assert.equal(f.nodes.upload.disabled,true);
  f.nodes.file.files=[{name:'firmware.bin',size:8000000}];f.nodes.file.events.change();assert.equal(f.nodes.upload.disabled,true);
  f.nodes.file.files=[{name:'firmware.bin',size:4096}];f.nodes.file.events.change();assert.equal(f.nodes.upload.disabled,false);
  f=await fixture([{error:403}]);assert.match(f.nodes.status.textContent,/administrator password/);assert.equal(f.nodes.file.disabled,true);
  f=await fixture([ready],{status:400,responseText:'Image rejected'});
  f.nodes.file.files=[{name:'firmware.bin',size:4096}];f.nodes.upload.events.click();await f.settle();
  assert.equal(f.nodes.status.textContent,'Image rejected');assert.equal(f.nodes.file.disabled,false);
  f=await fixture([ready,{...ready,token:'after',build:'bbbb',pending:true},{...ready,token:'after',build:'bbbb'}]);
  f.nodes.file.files=[{name:'firmware.bin',size:4096}];f.nodes.upload.events.click();await f.settle();
  assert.equal(f.sent().headers['X-OTA-Token'],'before');assert.equal(f.sent().headers['Content-Type'],'application/octet-stream');
  assert.match(f.nodes.status.textContent,/Update complete/);assert.equal(f.nodes.progress.value,100);
  f=await fixture([ready,{...ready,token:'after'}]);f.nodes.file.files=[{name:'firmware.bin',size:4096}];f.nodes.upload.events.click();await f.settle();
  assert.match(f.nodes.status.textContent,/rolled back/);
  console.log('Firmware page checks passed: initial state, limits, password setup, errors, progress, restart and rollback result');
})().catch(error=>{console.error(error);process.exitCode=1;});
