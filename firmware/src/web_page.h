// Configuration page served at GET /. Original markup, styling and script; the form field names,
// the %PLACEHOLDER% tokens and the endpoints are the contract with web.cpp (render_index,
// handle_save, handle_status) and must stay in step with it.
#pragma once
#include <Arduino.h>
static const char WEB_PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BT Terminal setup</title>
<style>
:root{--bg:#14171c;--card:#1d222a;--line:#2e3540;--fg:#dfe5ec;--dim:#8a95a3;--acc:#4ea3e0;--ok:#4cc38a;--bad:#e5645b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:640px;margin:0 auto;padding:12px 12px 90px}
h1{font-size:20px;margin:14px 0 2px}
.sub{color:var(--dim);font-size:12px;margin-bottom:12px}
fieldset{background:var(--card);border:1px solid var(--line);border-radius:8px;margin:0 0 12px;padding:10px 14px 12px}
legend{padding:0 6px;color:var(--acc);font-weight:600;letter-spacing:.03em}
label{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:8px 0}
label.chk{justify-content:flex-start}
label>span{flex:1}
input[type=text],input[type=number],input[type=time],select{background:#10141a;color:var(--fg);border:1px solid var(--line);border-radius:5px;padding:6px 8px;font:inherit;min-width:0}
input[type=number]{width:110px}
input[type=text]{flex:1}
input[type=range]{flex:1;max-width:260px;accent-color:var(--acc)}
input[type=checkbox],input[type=radio]{accent-color:var(--acc);width:18px;height:18px}
.row{display:flex;gap:14px;flex-wrap:wrap;margin:8px 0}
.row label{margin:0}
button{background:var(--acc);color:#04121f;border:0;border-radius:5px;padding:8px 14px;font:inherit;font-weight:600;cursor:pointer}
button.alt{background:#2b3440;color:var(--fg)}
button.bad{background:var(--bad);color:#fff}
button:disabled{opacity:.5}
.bar{position:fixed;left:0;right:0;bottom:0;background:#0d1015ee;border-top:1px solid var(--line);padding:10px;display:flex;gap:10px;justify-content:center;align-items:center}
#msg{min-width:90px;font-size:13px}
.ok{color:var(--ok)}.err{color:var(--bad)}
#sort{margin:6px 0}
.sort-item{display:flex;align-items:center;gap:10px;background:#10141a;border:1px solid var(--line);border-radius:5px;padding:6px 8px;margin:4px 0;cursor:grab;user-select:none}
.sort-item.drag{opacity:.4}
.sort-pos{width:22px;text-align:center;color:var(--dim)}
.sort-item .mv{margin-left:auto;display:flex;gap:4px}
.sort-item .mv button{padding:2px 9px;background:#2b3440;color:var(--fg)}
.ips div{display:flex;gap:6px;margin:4px 0}
dl{display:grid;grid-template-columns:auto 1fr;gap:3px 12px;margin:0}
dt{color:var(--dim)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums}
.hint{color:var(--dim);font-size:12px}
</style>
</head>
<body>
<main>
<h1>BT Terminal</h1>
<div class="sub">Firmware %FIRMWARE_VERSION% &middot; MAC %MAC%</div>

<fieldset><legend>Live</legend>
<dl id="live"><dt>Status</dt><dd>loading&hellip;</dd></dl>
</fieldset>

<form id="f" onsubmit="return false">

<fieldset><legend>Price</legend>
<div class="row">
<label class="chk"><input type="radio" name="currency" value="USD"%USD_SELECTED%> USD</label>
<label class="chk"><input type="radio" name="currency" value="EUR"%EUR_SELECTED%> EUR</label>
<label class="chk"><input type="radio" name="currency" value="CHF"%CHF_SELECTED%> CHF</label>
</div>
<label><span>Update interval (ms)</span><input type="number" name="priceInterval" min="1000" step="500" value="%PRICE_INTERVAL%"></label>
<label class="chk"><input type="checkbox" name="priceSeparator"%PRICESEPARATOR_ON%> <span>Thousands separator</span></label>
<label class="chk"><input type="checkbox" name="priceSplitScreen" id="split"%PRICESPLITSCREEN_ON%> <span>Split screen with a second currency</span></label>
<label id="splitcur" style="%SECONDARY_CURRENCY_DISPLAY%"><span>Second currency</span>
<select name="splitSecondaryCurrency">
<option value=""%SPLITSEC_AUTO_SEL%>Automatic</option>
<option value="USD"%SPLITSEC_USD_SEL%>USD</option>
<option value="EUR"%SPLITSEC_EUR_SEL%>EUR</option>
<option value="CHF"%SPLITSEC_CHF_SEL%>CHF</option>
</select></label>
</fieldset>

<fieldset><legend>Pages</legend>
<div class="hint">Drag to reorder, or use the arrows. Pages are switched on and off on the device itself.</div>
<div id="sort">%PAGE_SORT_ITEMS%</div>
<label class="chk"><input type="checkbox" name="showYoutubeBar"%YOUTUBE_ON%> <span>Show the video bar</span></label>
<label class="chk"><input type="checkbox" name="showEvents"%EVENTS_ON%> <span>Show events</span></label>
</fieldset>

<fieldset><legend>Display</legend>
<label><span>Brightness</span><input type="range" name="brightness" min="5" max="100" value="%BRIGHTNESS%"></label>
<label class="chk"><input type="checkbox" name="rotation"%ROTATION_CHECKED%> <span>Rotate screen by 180&deg;</span></label>
<label><span>Time zone</span><select name="timezone"></select></label>
</fieldset>

<fieldset><legend>Night mode</legend>
<label class="chk"><input type="checkbox" name="nightmodeActive"%NIGHTMODE_ACTIVE_ON%> <span>Dim the screen at night</span></label>
<div class="row">
<label><span>From&nbsp;</span><input type="time" name="nightmodeStart" value="%NIGHTMODE_START%"></label>
<label><span>To&nbsp;</span><input type="time" name="nightmodeStop" value="%NIGHTMODE_STOP%"></label>
</div>
<label><span>Delay before dimming (s)</span><input type="number" name="nightmodeDelay" min="0" value="%NIGHTMODE_DELAY%"></label>
</fieldset>

<fieldset><legend>Sound and light</legend>
<label><span>Buzzer volume</span><input type="range" name="buzzerVolume" min="0" max="100" value="%BUZZER_VOLUME%"></label>
<label class="chk"><input type="checkbox" name="ledEnabled"%LED_ENABLED%> <span>LED strip</span></label>
<label><span>LED brightness</span><input type="range" name="ledBrightness" min="0" max="100" value="%LED_BRIGHTNESS%"></label>
<label><span>LED type</span>
<select name="ledStripType">
<option value="0"%LED_STRIP_0%>WS2812 (GRB)</option>
<option value="1"%LED_STRIP_1%>WS2811 (RGB)</option>
</select></label>
</fieldset>

<fieldset><legend>Price alert</legend>
<label class="chk"><input type="checkbox" name="alertEnabled"%ALERT_ENABLED%> <span>Alert when the price leaves the range</span></label>
<label><span>Currency</span>
<select name="alertCurrency">
<option value="USD"%ALERT_USD_SEL%>USD</option>
<option value="EUR"%ALERT_EUR_SEL%>EUR</option>
<option value="CHF"%ALERT_CHF_SEL%>CHF</option>
</select></label>
<label><span>Above (0 = off)</span><input type="number" name="alertAbove" min="0" value="%ALERT_ABOVE%"></label>
<label><span>Below (0 = off)</span><input type="number" name="alertBelow" min="0" value="%ALERT_BELOW%"></label>
</fieldset>

<fieldset><legend>Bitaxe miners</legend>
<div class="ips" id="ips"></div>
<button type="button" class="alt" id="addip">Add miner</button>
</fieldset>

<fieldset><legend>System</legend>
<label class="chk"><input type="checkbox" name="checkForUpdates"%UPDATES_ON%> <span>Check for firmware updates</span></label>
<label class="chk"><input type="checkbox" name="debug"%DEBUG_ON%> <span>Debug mode (serial log, enables /update)</span></label>
<div class="row">
<button type="button" class="alt" id="ota">Check for update</button>
<button type="button" class="alt" id="restart">Restart</button>
<button type="button" class="bad" id="reset">Factory reset</button>
</div>
<div class="hint" id="otamsg"></div>
<div class="hint"><a href="/diag" style="color:var(--acc)">Diagnostics log</a></div>
</fieldset>

</form>
</main>

<div class="bar"><button id="save">Save</button><span id="msg"></span></div>

<script>
const BITAXE_IPS=[%BITAXE_IPS%];
const $=s=>document.querySelector(s), form=$('#f'), msg=$('#msg');

function say(t,cls){msg.textContent=t;msg.className=cls||'';if(t)setTimeout(()=>{if(msg.textContent===t)msg.textContent=''},4000)}
async function call(url,opt){const r=await fetch(url,opt);if(!r.ok)throw new Error(r.status);return r.json()}

// --- miners ---
function addIp(v){
  const d=document.createElement('div');
  d.innerHTML='<input type="text" name="bitaxeIP" placeholder="192.168.1.50"><button type="button" class="alt">&times;</button>';
  d.firstChild.value=v||'';
  d.lastChild.onclick=()=>{d.remove();if(!$('#ips').children.length)addIp('')};
  $('#ips').appendChild(d);
}
(BITAXE_IPS.length?BITAXE_IPS:['']).forEach(addIp);
$('#addip').onclick=()=>addIp('');

// --- page order ---
const sort=$('#sort');
function renumber(){[...sort.children].forEach((e,i)=>e.querySelector('.sort-pos').textContent=i+1)}
let held=null;
[...sort.children].forEach(it=>{
  const mv=document.createElement('span');mv.className='mv';
  [['▲',-1],['▼',1]].forEach(([c,d])=>{
    const b=document.createElement('button');b.type='button';b.textContent=c;
    b.onclick=()=>{
      const sib=d<0?it.previousElementSibling:it.nextElementSibling;
      if(sib){d<0?sort.insertBefore(it,sib):sort.insertBefore(sib,it);renumber()}
    };
    mv.appendChild(b);
  });
  it.appendChild(mv);
  it.ondragstart=e=>{held=it;it.classList.add('drag');e.dataTransfer.effectAllowed='move';e.dataTransfer.setData('text/plain','')};
  it.ondragend=()=>{it.classList.remove('drag');held=null;renumber()};
  it.ondragover=e=>{
    e.preventDefault();
    if(!held||held===it)return;
    const r=it.getBoundingClientRect();
    sort.insertBefore(held,e.clientY<r.top+r.height/2?it:it.nextSibling);
  };
});

// --- time zones: the firmware fills the options, nothing to do here ---
$('#split').onchange=e=>{$('#splitcur').style.display=e.target.checked?'':'none'};

// --- save ---
$('#save').onclick=async()=>{
  const body=new URLSearchParams(new FormData(form));
  body.set('pageOrder',[...sort.children].map(e=>e.dataset.key).join(','));
  try{await call('/save',{method:'POST',body});say('Saved','ok')}catch(e){say('Save failed','err')}
};

// --- system buttons ---
$('#restart').onclick=async()=>{
  if(!confirm('Restart the device?'))return;
  try{await fetch('/restart')}catch(e){}
  say('Restarting…');
};
$('#reset').onclick=async()=>{
  if(!confirm('Erase ALL settings, including Wi-Fi? The device restarts.'))return;
  try{await call('/reset');await fetch('/restart')}catch(e){}
  say('Resetting…');
};
$('#ota').onclick=async()=>{
  const o=$('#otamsg');o.textContent='Checking…';
  try{
    const r=await call('/ota');
    if(r.busy){o.textContent='An update is already running.';return}
    if(!r.available){o.textContent='Up to date ('+r.current+').';return}
    if(confirm('Install '+r.remote+' (now '+r.current+')?')){
      await call('/ota?install=1');o.textContent='Installing, the device restarts when done.';
    }else o.textContent='Update '+r.remote+' available.';
  }catch(e){o.textContent='Check failed.'}
};

// --- live values ---
function row(k,v){return '<dt>'+k+'</dt><dd>'+v+'</dd>'}
async function live(){
  try{
    const s=await call('/status.json');
    let h=row('Time',s.clock.weekday+' '+s.clock.date+' '+s.clock.time);
    h+=row('Price',s.price.usd+' USD / '+s.price.eur+' EUR / '+s.price.chf+' CHF');
    h+=row('24h',s.price.chg_usd+'% / '+s.price.chg_eur+'% / '+s.price.chg_chf+'%');
    h+=row('Block',s.block.height);
    h+=row('Fees (sat/vB)',s.fees.low+' / '+s.fees.mid+' / '+s.fees.high);
    h+=row('Difficulty',s.difficulty.progress+'% done, '+s.difficulty.change+'% next, '+s.difficulty.blocks+' blocks left');
    s.bitaxe.forEach(b=>{h+=row(b.name,b.hash+', '+b.temp+', fan '+b.fan)});
    $('#live').innerHTML=h;
  }catch(e){}
}
live();setInterval(live,5000);
</script>
</body>
</html>
)HTML";
