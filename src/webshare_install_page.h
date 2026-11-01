#pragma once

// The X's page (webshare_install.cpp, docs/apps.md "The X's page"): Apps (install, remove, configure), Devices
// (printer, Bluetooth, USB and their settings), This X. Names and values are shown with textContent only.
static const char WEBSHARE_INSTALL_PAGE[] = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>The X</title>
<style>
:root{--fg:#000;--bg:#f4f4f2;--card:#fff;--line:#d9d9d6;--mute:#666;--ok:#e8f3e8;--bad:#fbe3e3}
@media(prefers-color-scheme:dark){:root{--fg:#f2f2f2;--bg:#101010;--card:#1b1b1b;--line:#333;--mute:#aaa;--ok:#173017;--bad:#3a1616}}
*{box-sizing:border-box}
body{font:16px/1.45 Inter,system-ui,sans-serif;margin:0;color:var(--fg);background:var(--bg)}
header{background:#000;color:#fff;padding:.9rem 1rem;font-weight:700;letter-spacing:.02em;display:flex;gap:.6rem;align-items:center}
header b{border:2px solid #fff;border-radius:.35rem;padding:0 .35rem}
main,#login{max-width:40rem;margin:0 auto;padding:1rem}
nav{display:flex;gap:.4rem;margin-bottom:.8rem}nav button{flex:1}
nav button.on{background:var(--fg);color:var(--bg)}
section{background:var(--card);border:1px solid var(--line);border-radius:.6rem;padding:.8rem 1rem;margin-bottom:.8rem}
h2{font-size:1.05rem;margin:.1rem 0 .6rem}
button,input,select{font:inherit;padding:.45rem .75rem;border:2px solid var(--fg);border-radius:.45rem;background:var(--card);color:var(--fg)}
button{font-weight:600;cursor:pointer}input,select{max-width:100%}
.row{display:flex;gap:.6rem;align-items:center;border-top:1px solid var(--line);padding:.55rem 0;flex-wrap:wrap}
.row:first-child{border-top:0}.n{flex:1;min-width:8rem;word-break:break-word}.s{color:var(--mute);font-size:.85rem}
canvas{width:40px;height:40px;border:1px solid var(--line);border-radius:.35rem;image-rendering:pixelated;background:#fff}
.f{display:flex;flex-direction:column;gap:.2rem;margin:.5rem 0}.f label{font-weight:600;font-size:.9rem}
.key{font:700 2.4rem/1.2 ui-monospace,monospace;letter-spacing:.3em;text-align:center;padding:.4rem 0}
#nets{margin-top:.6rem}
.warn{border-left:4px solid var(--fg);padding:.3rem .7rem;margin:.6rem 0;font-size:.92rem}
#msg{white-space:pre-line;margin:.6rem 0;padding:.6rem .8rem;background:var(--ok);border-radius:.45rem}#msg.bad{background:var(--bad)}
</style></head><body>
<header><b>X</b><span id=title>The X</span></header>
<div id=login><p>Type the 6-digit code shown on the X.</p>
<input id=pin inputmode=numeric pattern="[0-9]*" maxlength=6 autocomplete=off size=8> <button id=go>Open</button></div>
<main id=main hidden>
<nav><button data-t=apps class=on>Apps</button><button data-t=devs>Devices</button><button data-t=me>This X</button></nav>
<div id=msg hidden></div>
<div id=t-apps>
<section><h2>Apps</h2><div id=apps></div></section>
<section id=cfg hidden><h2 id=cfgname></h2><div class=f><label>Close this app after</label><select id=cfgidle></select></div>
<form id=cfgform></form><button id=cfgsave>Save</button>
<p class=s>The app reads the new values when it starts next (or at once if it runs).</p></section>
<section><h2>Install or update</h2><input id=up type=file accept=".elf" multiple>
<p class=s>Every file is checked before it is kept. An app with the same name is kept as .elf.bak until the new one has
started once. From a computer: <code>curl -F file=@app.elf "http://&lt;this address&gt;/apps?code=&lt;code&gt;"</code></p></section>
</div>
<div id=t-devs hidden>
<section><h2>Printer</h2><div id=prn></div><button id=find>Find printers</button><div id=found></div></section>
<section><h2>Bluetooth</h2><div id=ble></div><div id=pair></div></section>
<section><h2>USB</h2><div id=usb></div></section>
<section><h2>Drawing</h2><p class=s>Pen tablet and mouse, and the clean-up flash of the screen.</p><div id=devset></div></section>
</div>
<div id=t-me hidden>
<section><h2>This X</h2><div id=me></div><p id=kbd></p></section>
<section><h2>Apps</h2><p class=s>An app that gets no touch, key or pen input goes back to TRMNL after this long.</p><div id=idle></div></section>
<section><h2>Wi-Fi</h2><div id=wifi></div><button id=scan>Change network</button><div id=nets></div></section>
<section><h2>Touch bar</h2><div id=bar><button data-b=left>Left</button> <button data-b=middle>Middle</button>
<button data-b=right>Right</button> <button data-b=long>Long press</button> <button data-b=stop>Stop page</button></div></section>
</div>
</main>
<script>
const $=id=>document.getElementById(id),H={'X-Writer':'1'},F={'Content-Type':'application/x-www-form-urlencoded'};
const el=(t,txt,cls)=>{const e=document.createElement(t);if(txt!=null)e.textContent=txt;if(cls)e.className=cls;return e};
function say(t,bad){const m=$('msg');m.textContent=t;m.className=bad?'bad':'';m.hidden=!t}
function show(ok){$('login').hidden=ok;$('main').hidden=!ok}
async function api(path,opt){
  try{const r=await fetch(path,Object.assign({headers:H,credentials:'same-origin'},opt||{}));
    if(r.status==401)show(false);
    let j={};try{j=await r.json()}catch(e){}
    return{ok:r.ok,status:r.status,j}}
  catch(e){return{ok:false,status:0,j:{error:'no connection (the X switched the page off?)'}}}}
const post=(p,b)=>api(p,{method:'POST',body:b||'',headers:Object.assign({},F,H)});
const enc=o=>Object.keys(o).map(k=>k+'='+encodeURIComponent(o[k])).join('&');
function kb(n){return n<1024?n+' B':(n/1024).toFixed(1)+' KB'}
function icon(c,hex){const g=c.getContext('2d');c.width=c.height=64;g.fillStyle='#fff';g.fillRect(0,0,64,64);g.fillStyle='#000';
  for(let i=0;i<hex.length/2;i++){const b=parseInt(hex.substr(i*2,2),16);for(let k=0;k<8;k++)if(!(b&(128>>k)))g.fillRect((i%8)*8+k,i>>3,1,1)}}
function letter(c,t){const g=c.getContext('2d');c.width=c.height=64;g.fillStyle='#000';g.font='bold 40px sans-serif';g.textAlign='center';g.fillText(t,32,46)}
function tab(t){document.querySelectorAll('nav button').forEach(b=>b.className=b.dataset.t==t?'on':'');
  ['apps','devs','me'].forEach(x=>$('t-'+x).hidden=x!=t);say('');if(t=='devs')devices();if(t=='me')me()}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>tab(b.dataset.t));
async function load(){
  const r=await api('/api/apps');if(!r.ok){if(r.status!=401)say(r.j.error||'Failed',true);return}
  show(true);window.kbd&&kbd();const box=$('apps');box.textContent='';
  if(r.j.note)say(r.j.note,true);
  for(const a of r.j.apps){
    const id=a.n.replace(/\.elf$/,''),d=el('div',null,'row'),c=el('canvas');d.appendChild(c);letter(c,id[0].toUpperCase());
    const n=el('span',id+(a.bak?' (older version kept)':''),'n');d.appendChild(n);
    d.appendChild(el('span',kb(a.s),'s'));
    const cf=el('button','Configure');cf.onclick=()=>configure(id,n.textContent);d.appendChild(cf);
    const x=el('button','Remove');x.onclick=async()=>{
      if(!confirm('Remove '+n.textContent+' from the X?'))return;
      const res=await post('/api/appdelete?name='+encodeURIComponent(a.n));
      say(res.ok?'Removed.':(res.j.error||'Failed'),!res.ok);load()};d.appendChild(x);box.appendChild(d);
    api('/api/appinfo?app='+encodeURIComponent(id)).then(i=>{if(!i.ok)return;n.textContent=i.j.name;
      if(i.j.icon)icon(c,i.j.icon);else letter(c,(i.j.name[0]||'?').toUpperCase())})}
  if(!r.j.apps.length)box.textContent='No apps yet.';go()}
// The section an app asked for (device_page_open: #configure=<app>, #devices, #this-x, #apps), once
function go(){const h=decodeURIComponent(location.hash.slice(1));if(!h)return;history.replaceState(null,'',location.pathname);
  if(h.startsWith('configure='))configure(h.slice(10),h.slice(10));else if(h=='devices')tab('devs');else if(h=='this-x')tab('me')}
let cfgApp='';
async function configure(id,name){
  const r=await api('/api/appsettings?app='+encodeURIComponent(id));
  if(!r.ok&&r.status!=404){say(r.j.error||'Failed',true);return}
  cfgApp=id;$('cfg').hidden=false;idleLoad(id);$('cfgname').textContent=name+' settings';const f=$('cfgform');f.textContent='';
  for(const x of r.ok?r.j.fields:[]){const w=el('div',null,'f');w.appendChild(el('label',x.l));let i;
    if(x.t=='choice'){i=el('select');x.choices.forEach(c=>{const o=el('option',c);o.value=c;i.appendChild(o)});i.value=x.v}
    else{i=el('input');i.type=x.t=='secret'?'password':x.t=='number'?'number':'text';
      if(x.t=='secret')i.placeholder=x.set?'(set; type to change)':'(not set)';else i.value=x.v||''}
    i.name='v_'+x.k;i.dataset.secret=x.t=='secret'?1:'';w.appendChild(i);f.appendChild(w)}
  $('cfg').scrollIntoView()}
const IDLE=['default','off','5','10','15','30','60'],idleName=c=>c=='default'?'Use the device setting':c=='off'?'Never':c+' minutes';
async function idleLoad(id){const i=$('cfgidle'),r=await api('/api/appidle?app='+encodeURIComponent(id));i.textContent='';
  IDLE.forEach(c=>{const o=el('option',idleName(c));o.value=c;i.appendChild(o)});i.value=r.ok?r.j.v:'default';
  i.onchange=async()=>{const x=await post('/api/appidle?app='+encodeURIComponent(id),enc({v:i.value}));say(x.ok?'Saved.':(x.j.error||'Failed'),!x.ok)}}
$('cfgsave').onclick=async()=>{const o={};
  $('cfgform').querySelectorAll('[name]').forEach(i=>{if(i.dataset.secret&&!i.value)return;o[i.name]=i.value});
  const r=await post('/api/appsettings?app='+encodeURIComponent(cfgApp),enc(o));
  say(r.ok?'Saved ('+r.j.changed+' changed).':(r.j.error||'Failed'),!r.ok)};
async function setdev(k,v){const r=await post('/api/device',enc({k,v}));say(r.ok?'Saved.':(r.j.error||'Failed'),!r.ok);if(r.ok)devices()}
const pv=t=>{const o={};(t||'').split('\n').forEach(l=>{const i=l.indexOf('=');if(i>0)o[l.slice(0,i)]=l.slice(i+1)});return o};
async function devices(){
  const r=await api('/api/devices');if(!r.ok){say(r.j.error||'Failed',true);return}
  const p=$('prn'),ds=$('devset');p.textContent='';ds.textContent='';
  for(const s of r.j.settings){
    if(s.t=='printer'){const v=pv(s.v),d=el('div',null,'row');
      d.appendChild(el('span',v.host?'Default: '+(v.name||v.host)+' ('+v.host+')':'No default printer yet.','n'));
      if(v.host){const t=el('button','Test page');t.onclick=async()=>{if(!confirm('Print a test page on '+(v.name||v.host)+'? The X closes this page and prints.'))return;
        const x=await post('/api/printertest');say(x.ok?'The X prints the test page now.':(x.j.error||'Failed'),!x.ok)};d.appendChild(t);
        const c=el('button','Clear');c.onclick=()=>setdev('printer','');d.appendChild(c)}
      p.appendChild(d);continue}
    if(s.k=='app_idle_min')continue;
    const w=el('div',null,'f');w.appendChild(el('label',s.l));let i;
    if(s.choices){i=el('select');s.choices.split('|').forEach(c=>{const o=el('option',c);o.value=c;i.appendChild(o)})}
    else{i=el('input');i.type='number';i.min=s.min;i.max=s.max}
    i.value=s.v;i.onchange=()=>setdev(s.k,i.value);w.appendChild(i);ds.appendChild(w)}
  const b=$('ble');b.textContent='';const d=el('div',null,'row');
  d.appendChild(el('span',r.j.ble?'Paired keyboard: '+r.j.ble:'No Bluetooth keyboard paired.','n'));
  if(r.j.ble){const f=el('button','Forget');f.onclick=async()=>{if(!confirm('Forget '+r.j.ble+'?'))return;
    const x=await post('/api/bleforget');say(x.ok?'Forgotten.':(x.j.error||'Failed'),!x.ok);devices()};d.appendChild(f)}
  b.appendChild(d);if(!pairing){const pk=el('button','Pair a keyboard');pk.onclick=()=>blepair('start');b.appendChild(pk);
    b.appendChild(el('p','Switch the keyboard to pairing first. The X lists what it finds here and on its screen.','s'))}
  const u=$('usb');u.textContent='';
  r.j.usb.forEach(x=>{const d=el('div',null,'row');d.appendChild(el('span',x.kind,'n'));d.appendChild(el('span',x.id,'s'));u.appendChild(d)});
  if(!r.j.usb.length)u.appendChild(el('p','No USB device seen since the X started.','s'))}
// Bluetooth pairing: the X scans, the page lists, the X shows the passkey; checked every 1.5 s while it runs
let pairing=false,ptimer=0;
async function blepair(what,i){const r=await post('/api/blepair',enc(i==null?{do:what}:{do:what,i}));
  if(!r.ok){say(r.j.error||'Failed',true);return}if(what=='start'){pairing=true;devices();blepoll()}
  if(what=='stop'){pairing=false;$('pair').textContent='';devices()}}
async function blepoll(){clearTimeout(ptimer);if(!pairing)return;const r=await api('/api/blepair');const p=$('pair');p.textContent='';
  if(!r.ok){say(r.j.error||'Failed',true);return}const s=r.j;
  if(s.state=='paired'){say('Paired with '+s.paired+'. Apps that use a keyboard find it now.');blepair('stop');return}
  if(s.state=='off'){pairing=false;devices();return}
  if(s.message)p.appendChild(el('p',s.message,'s'));
  if(s.state=='list'){p.appendChild(el('p',s.found.length?'Keyboards found:':'Looking for keyboards...','s'));
    s.found.forEach((k,i)=>{const d=el('div',null,'row');d.appendChild(el('span',k.name||'(no name)','n'));d.appendChild(el('span',k.rssi+' dBm','s'));
      const b=el('button','Pair');b.onclick=()=>blepair('pick',i);d.appendChild(b);p.appendChild(d)})}
  if(s.state=='connecting')p.appendChild(el('p','Connecting...','s'));
  if(s.state=='passkey'){p.appendChild(el('p','Type this on the keyboard, then press Enter:'));p.appendChild(el('div',s.passkey,'key'))}
  const x=el('button','Stop');x.onclick=()=>blepair('stop');p.appendChild(x);ptimer=setTimeout(blepoll,1500)}
// Wi-Fi: scan, pick, password (never shown back), Join after a confirm; the X tries it once this page has closed
let wifiNow='';
$('scan').onclick=async()=>{const n=$('nets');n.textContent='Scanning (a few seconds)...';const r=await post('/api/wifiscan');n.textContent='';
  if(!r.ok){say(r.j.error||'Failed',true);return}
  if(!r.j.nets.length)n.appendChild(el('p','No network found.','s'));
  r.j.nets.forEach(w=>{const d=el('div',null,'row');d.appendChild(el('span',w.ssid,'n'));
    d.appendChild(el('span',w.band+' GHz, '+w.rssi+' dBm'+(w.open?', open':''),'s'));
    if(w.ent){d.appendChild(el('span','enterprise: use the setup portal','s'));n.appendChild(d);return}
    const b=el('button','Choose');b.onclick=()=>pickwifi(w);d.appendChild(b);n.appendChild(d)})};
function pickwifi(w){const n=$('nets');n.textContent='';n.appendChild(el('h2',w.ssid));let pw=null;
  if(!w.open){const f=el('div',null,'f');f.appendChild(el('label','Password'));pw=el('input');pw.type='password';pw.autocomplete='off';
    pw.maxLength=64;f.appendChild(pw);n.appendChild(f)}
  n.appendChild(el('p','What happens: the X closes this page and joins "'+w.ssid+'". If that works it keeps it and opens the page again on that network: put your phone on "'+w.ssid+'" too and scan the new code. If it does not work, the X stays on '+(wifiNow?'"'+wifiNow+'"':'its saved network')+'. Either way the result is on the X\'s screen.','warn'));
  const j=el('button','Join');j.onclick=async()=>{if(!confirm('Close this page and switch the X to "'+w.ssid+'"?'))return;
    const r=await post('/api/wifijoin',enc({s:w.ssid,p:pw?pw.value:''}));if(pw)pw.value='';
    say(r.ok?'The X is switching to "'+w.ssid+'". This page stops now; look at the X.':(r.j.error||'Failed'),!r.ok)};
  const c=el('button','Cancel');c.onclick=()=>{n.textContent=''};n.appendChild(j);n.appendChild(document.createTextNode(' '));n.appendChild(c)}
$('find').onclick=async()=>{$('found').textContent='Searching...';const r=await post('/api/printers');const f=$('found');f.textContent='';
  if(!r.ok){say(r.j.error||'Failed',true);return}
  if(!r.j.printers.length)f.appendChild(el('p','No printer found on this network.','s'));
  r.j.printers.forEach(x=>{const d=el('div',null,'row');d.appendChild(el('span',x.name||x.host,'n'));d.appendChild(el('span',x.host,'s'));
    const u=el('button','Use');u.onclick=()=>setdev('printer','name='+x.name+'\nhost='+x.host+'\nport='+x.port+'\npath='+(x.path||'ipp/print')+'\nformat=pwg');
    d.appendChild(u);f.appendChild(d)})};
async function me(){const r=await api('/api/x');if(!r.ok)return;const m=$('me');m.textContent='';
  $('title').textContent=r.j.name;wifiNow=r.j.wifi||'';
  [['Name',r.j.name],['Firmware',r.j.version]].forEach(([k,v])=>{
    const d=el('div',null,'row');d.appendChild(el('span',k,'n'));d.appendChild(el('span',v));m.appendChild(d)});
  const w=$('wifi');w.textContent='';const d=el('div',null,'row');d.appendChild(el('span','Saved network','n'));
  d.appendChild(el('span',r.j.wifi?r.j.wifi+(r.j.wifi5?' (5 GHz)':' (2.4 GHz)'):'none saved'));w.appendChild(d);
  const dv=await api('/api/devices'),b=$('idle');b.textContent='';
  const s=dv.ok&&dv.j.settings.find(x=>x.k=='app_idle_min');if(s){const f=el('div',null,'f');f.appendChild(el('label',s.l));
    const i=el('select');s.choices.split('|').forEach(c=>{const o=el('option',c=='off'?'Never':c+' minutes');o.value=c;i.appendChild(o)});
    i.value=s.v;i.onchange=async()=>{const x=await post('/api/device',enc({k:s.k,v:i.value}));say(x.ok?'Saved.':(x.j.error||'Failed'),!x.ok)};
    f.appendChild(i);b.appendChild(f)}}
$('go').onclick=async()=>{
  const r=await post('/api/login','pin='+encodeURIComponent($('pin').value));
  if(r.ok){say('');load();me()}else say(r.j.error||'Failed',true)};
document.querySelectorAll('#bar button').forEach(b=>b.onclick=async()=>{
  const r=await post('/api/touch','b='+b.dataset.b);
  say(r.ok?(b.dataset.b=='stop'||b.dataset.b=='long'?'Sent. The X stops this page.':'Sent.'):(r.j.error||'Failed'),!r.ok)});
$('pin').onkeydown=e=>{if(e.key=='Enter')$('go').click()};
$('up').onchange=async e=>{
  const out=[];
  for(const f of e.target.files){
    const r=await api('/api/app?name='+encodeURIComponent(f.name),{method:'POST',body:f});
    out.push(f.name+': '+(r.ok?'installed':'refused: '+(r.j.error||'failed')))}
  say(out.join('\n'));e.target.value='';load()};
load();me();
</script><script src=/kbd.js></script></body></html>)HTML";
