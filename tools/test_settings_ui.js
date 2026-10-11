#!/usr/bin/env node
// Browser checks use HTML emitted by the production C handler, not a mock layout.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {chromium}=require(process.env.PANEL_PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync('python',[path.join(__dirname,'render_web_config.py')],{encoding:'utf8'});
const firmware=fs.readFileSync(path.join(root,'main/firmware.html'),'utf8');
const output=process.env.PANEL_UI_OUTPUT||'/tmp/panel-settings-check';fs.mkdirSync(output,{recursive:true});
(async()=>{
 const browser=await chromium.launch({headless:true});
 const context=await browser.newContext({viewport:{width:1280,height:950}});
 let sent=null,failSave=false;const errors=[];
 await context.route('http://panel.test/**',route=>{
  const request=route.request(),url=new URL(request.url());
  if(url.pathname==='/save'){sent=new URLSearchParams(request.postData());return route.fulfill({status:failSave?400:200,contentType:'text/html',body:failSave?'Invalid configuration':'<h2>Configuration saved</h2>'});}
  if(url.pathname==='/firmware/status')return route.fulfill({json:{version:'test',build:'aabbccdd',pending:false,max_size:7340032,token:'test'}});
  return route.fulfill({contentType:'text/html',body:url.pathname==='/firmware'?firmware:html});
 });
 const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.goto('http://panel.test/');
 assert.equal(await page.getByRole('tab').count(),6);
 assert.equal(await page.locator('[role=tabpanel]:visible').count(),1);
 assert(await page.locator('#save-settings').isDisabled());
 assert.equal(await page.locator('form form').count(),0);
 // All labelled controls have programmatic labels, including inactive tabs.
 assert.deepEqual(await page.locator('#settings-form input:not([type=hidden]),#settings-form select').evaluateAll(inputs=>inputs.filter(i=>!i.labels.length).map(i=>i.name)),[]);
 await page.getByRole('tab',{name:'Display',exact:true}).click();
 await page.locator('[name=display_title]').fill('Living room');
 await page.locator('[name=sleep_enabled]').selectOption('1');
 await page.locator('[name=auto_brightness]').selectOption('1');
 await page.getByRole('tab',{name:'Weather',exact:true}).click();
 await page.locator('[name=weather_enabled]').selectOption('1');
 // Provider-dependent visibility, requirements and payloads.
 assert(!(await page.locator('#weather-ambient-key').isVisible()));
 assert(!(await page.locator('#weather-wu-key').isVisible()));
 await page.locator('[name=weather_forecast]').selectOption('1');
 assert(await page.locator('#weather-wu-key').isVisible());
 assert(!(await page.locator('[name=weather_forecast_key]').evaluate(i=>i.required)));
 await page.locator('[name=weather_provider]').selectOption('1');
 assert(await page.locator('#weather-ambient-key').isVisible());
 assert(await page.locator('[name=weather_app_key]').evaluate(i=>i.required));
 assert(await page.locator('[name=weather_forecast_key]').evaluate(i=>i.required));
 assert.equal(await page.locator('#weather-station-label').textContent(),'Ambient station name or MAC address');
 await page.locator('[name=weather_app_key]').fill('ambient-app');
 await page.locator('[name=weather_forecast_key]').fill('forecast-key');
 await page.locator('[name=weather_forecast]').selectOption('0');
 assert(!(await page.locator('#weather-wu-key').isVisible()));
 assert(!(await page.evaluate(()=>new FormData(document.getElementById('settings-form')).has('weather_forecast_key'))));
 await page.locator('[name=weather_forecast]').selectOption('2');
 assert((await page.locator('#weather-forecast-help').textContent()).includes('Only station observations'));
 await page.locator('[name=weather_forecast]').selectOption('1');
 assert.equal(await page.locator('[name=weather_forecast_key]').inputValue(),'forecast-key');
 await page.locator('[name=weather_provider]').selectOption('0');
 assert(!(await page.locator('#weather-ambient-key').isVisible()));
 assert(!(await page.evaluate(()=>new FormData(document.getElementById('settings-form')).has('weather_app_key'))));
 await page.locator('[name=weather_forecast]').selectOption('0');
 await page.locator('[name=weather_station]').fill('KTEST1');
 await page.locator('[name=weather_api_key]').fill('secret-key');
 await page.getByRole('tab',{name:'Sensors',exact:true}).click();
 await page.locator('[name=presence_enabled]').selectOption('1');
 await page.getByRole('tab',{name:'Network',exact:true}).click();
 assert.equal(await page.locator('[name=display_title]').inputValue(),'Living room');
 assert.equal(await page.locator('#change-status').textContent(),'Unsaved changes');
 await page.locator('#tab-network').press('End');assert.equal(await page.locator('#tab-system').getAttribute('aria-selected'),'true');
 await page.locator('#tab-system').press('Home');assert.equal(await page.locator('#tab-network').getAttribute('aria-selected'),'true');
 // Invalid fields in other tabs must be revealed before native error focus.
 await page.evaluate(()=>{const i=document.querySelector('[name=display_title]');i.value='';i.dispatchEvent(new Event('input',{bubbles:true}));});
 await page.locator('#save-settings').click();assert.equal(await page.locator('#tab-display').getAttribute('aria-selected'),'true');assert.equal(sent,null);
 await page.locator('[name=display_title]').fill('Living room');
 failSave=true;await page.locator('#save-settings').click();await page.locator('#save-notice.error').waitFor();
 assert.equal(await page.locator('[name=display_title]').inputValue(),'Living room');assert(!(await page.locator('[name=display_title]').isDisabled()));
 assert(!(await page.locator('#weather-ambient-key').isVisible()));assert(!(await page.locator('#weather-wu-key').isVisible()));
 assert.equal(sent.get('weather_enabled'),'1');assert.equal(sent.get('weather_station'),'KTEST1');assert.equal(sent.get('sleep_enabled'),'1');assert.equal(sent.get('auto_brightness'),'1');
 assert.equal(sent.get('presence_enabled'),'1');assert.equal(sent.get('csrf'),'test-csrf');assert.equal(sent.get('hostname'),'panel-010101');assert.equal(sent.get('display_title'),'Living room');
 await page.locator('#discard-settings').click();assert.equal(await page.locator('[name=display_title]').inputValue(),'Home Control');assert(await page.locator('#save-settings').isDisabled());
 await page.getByRole('tab',{name:'Network',exact:true}).click();await page.locator('[name=wifi_password]').fill('secret123');await page.getByRole('button',{name:'Show Password',exact:true}).click();assert.equal(await page.locator('[name=wifi_password]').getAttribute('type'),'text');
 await page.locator('#discard-settings').click();
 for(const width of [1280,390]){
  await page.setViewportSize({width,height:950});
  for(const id of ['network','display','home-assistant','weather','sensors','system']){
   await page.locator('#tab-'+id).click();
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow: ${width}/${id}`);
   await page.screenshot({path:path.join(output,`${id}-${width}.png`),fullPage:true});
  }
 }
 await page.locator('#tab-weather').click();
 await page.locator('[name=weather_enabled]').selectOption('1');
 for(const provider of ['0','1']){
  await page.locator('[name=weather_provider]').selectOption(provider);
  for(const forecast of ['0','1','2']){
   await page.locator('[name=weather_forecast]').selectOption(forecast);
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(output,`weather-provider-${provider}-forecast-${forecast}.png`),fullPage:true});
  }
 }
 await page.locator('#discard-settings').click();
 assert(await page.locator('[name=weather_provider]').isDisabled());
 assert(!(await page.locator('#weather-ambient-key').isVisible()));
 await page.goto('http://panel.test/#sensors');assert.equal(await page.locator('#tab-sensors').getAttribute('aria-selected'),'true');
 await page.locator('[name=presence_enabled]').selectOption('1');failSave=false;await page.locator('#save-settings').click();await page.getByRole('link',{name:'Reconnect to panel'}).waitFor();assert.equal(await page.locator('#change-status').textContent(),'Settings saved');assert(await page.locator('#save-settings').isDisabled());
 await page.goto('http://panel.test/firmware');await page.screenshot({path:path.join(output,'firmware-390.png'),fullPage:true});
 assert.deepEqual(errors,[]);
 const plain=await browser.newContext({javaScriptEnabled:false});await plain.route('**/*',r=>r.fulfill({contentType:'text/html',body:html}));const fallback=await plain.newPage();await fallback.goto('http://panel.test/');assert(await fallback.locator('[name=hostname]').isVisible());assert(await fallback.locator('[name=presence_address]').isVisible());assert(await fallback.locator('#save-settings').isEnabled());
 await browser.close();console.log('Settings browser checks passed: tabs, keyboard, labels, editing, validation, save errors, payloads, reset, mobile layout and no-JS fallback');
})().catch(e=>{console.error(e);process.exit(1);});
