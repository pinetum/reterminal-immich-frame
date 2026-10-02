#pragma once
#include <Arduino.h>

// The whole admin page lives here as one PROGMEM string. Keeping it out of a
// filesystem means there is no `pio run -t uploadfs` step to forget and no
// partition juggling -- the page is small and ships with the firmware.
static const char ADMIN_PAGE[] PROGMEM = R"HTMLPAGE(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Immich photo frame</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--ink:#14171a;--mute:#5b6570;--line:#dfe3e8;--accent:#2f6fed;--ok:#1a7f45;--bad:#c62f2f}
@media(prefers-color-scheme:dark){:root{--bg:#14171a;--card:#1d2126;--ink:#e9edf1;--mute:#9aa4af;--line:#2d333a;--accent:#6c9bff;--ok:#49c07b;--bad:#ff6b6b}}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--ink);font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:620px;margin:0 auto}
h1{font-size:20px;margin:4px 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mute);margin:0 0 12px}
label{display:block;font-size:13px;color:var(--mute);margin:12px 0 4px}
label:first-of-type{margin-top:0}
input,select{width:100%;padding:9px 10px;font-size:15px;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:8px}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:-1px}
.row{display:flex;gap:10px;flex-wrap:wrap}
.row>*{flex:1 1 140px}
button{padding:10px 14px;font-size:15px;font-weight:600;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--ink);cursor:pointer}
button.p{background:var(--accent);border-color:var(--accent);color:#fff}
button:disabled{opacity:.5;cursor:default}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}
.chk{display:flex;align-items:center;gap:8px;margin-top:12px}
.chk input{width:auto}
.chk label{margin:0}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:14px}
dt{color:var(--mute)}
dd{margin:0;text-align:right;font-variant-numeric:tabular-nums}
.msg{margin-top:12px;padding:10px 12px;border-radius:8px;font-size:14px;display:none}
.msg.ok{display:block;background:color-mix(in srgb,var(--ok) 14%,transparent);color:var(--ok)}
.msg.bad{display:block;background:color-mix(in srgb,var(--bad) 14%,transparent);color:var(--bad)}
.hint{font-size:12px;color:var(--mute);margin-top:6px}
.note{font-size:12px;color:var(--mute);margin-top:14px;text-align:center}
</style>
<div class=wrap>
<h1>Immich photo frame</h1>

<div class=card>
<h2>Status</h2>
<dl>
<dt>Photo</dt><dd id=s_pos>-</dd>
<dt>Battery</dt><dd id=s_bat>-</dd>
<dt>SD card</dt><dd id=s_sd>-</dd>
<dt>Cached frames</dt><dd id=s_cache>-</dd>
<dt>Last render</dt><dd id=s_render>-</dd>
<dt>Settings close in</dt><dd id=s_window>-</dd>
</dl>
<div class=msg id=s_err></div>
<div class=btns>
<button onclick="act('prev')">&#8592; Previous</button>
<button onclick="act('next')">Next &#8594;</button>
<button onclick="act('refresh')">Redraw</button>
</div>
<p class=hint>A full-colour refresh takes about 40 seconds. The panel keeps the
image with no power, so nothing is lost if you close this page.</p>
</div>

<form id=f>

<div class=card>
<h2>Wi-Fi</h2>
<label for=wifiSsid>Network</label>
<div class=row>
<input id=wifiSsid name=wifiSsid list=ssids autocomplete=off>
<button type=button style="flex:0 0 auto" onclick=scan()>Scan</button>
</div>
<datalist id=ssids></datalist>
<label for=wifiPass>Password</label>
<input id=wifiPass name=wifiPass type=password placeholder="leave blank to keep">
</div>

<div class=card>
<h2>Immich</h2>
<label for=immichUrl>Server address</label>
<input id=immichUrl name=immichUrl placeholder="http://192.168.1.50:2283">
<p class=hint>Just the origin &mdash; the <code>/api</code> part is added automatically.</p>
<label for=immichKey>API key</label>
<input id=immichKey name=immichKey type=password placeholder="leave blank to keep">
<p class=hint>Immich &rarr; Account Settings &rarr; API Keys.</p>
<div class=btns>
<button type=button onclick=test()>Test connection</button>
<button type=button onclick=albums()>Load albums</button>
</div>
<label for=albumSel>Album</label>
<select id=albumSel onchange=pickAlbum()><option value="">(load albums first)</option></select>
<label for=albumId>Album UUID</label>
<input id=albumId name=albumId placeholder="paste a UUID to set it directly">
<div class=msg id=i_msg></div>
</div>

<div class=card>
<h2>Slideshow</h2>
<label for=intervalMinutes>Change photo every (minutes)</label>
<input id=intervalMinutes name=intervalMinutes type=number min=1 max=10080>
<p class=hint>Longer is much kinder to the battery: each change costs about
2.5&nbsp;mAh, nearly all of it the panel refresh.</p>
<div class=chk><input id=shuffle name=shuffle type=checkbox><label for=shuffle>Shuffle order</label></div>
<label for=imageSize>Image size requested from Immich</label>
<select id=imageSize name=imageSize>
<option value=0>thumbnail (250 px &mdash; fast, soft)</option>
<option value=1>preview (recommended)</option>
<option value=2>fullsize</option>
<option value=3>original (slowest, highest quality)</option>
</select>
<p class=hint>Immich must serve these as JPEG or PNG. If your server is set to
WebP thumbnails, change it under Administration &rarr; Settings &rarr; Image Settings.</p>
<label for=playlistTtlHours>Re-read the album every (hours)</label>
<input id=playlistTtlHours name=playlistTtlHours type=number min=1 max=720>
</div>

<div class=card>
<h2>Picture</h2>
<label for=rotation>Orientation</label>
<select id=rotation name=rotation>
<option value=-1>Automatic &mdash; turn landscape photos (frame clockwise)</option>
<option value=0>Portrait 1200&times;1600</option>
<option value=270>Landscape 1600&times;1200 &mdash; frame turned clockwise</option>
<option value=90>Landscape 1600&times;1200 &mdash; frame turned anticlockwise</option>
<option value=180>Portrait, upside down</option>
</select>
<p class=hint>If landscape photos come out upside down, pick the other landscape
option &mdash; it depends which way round the frame is mounted.</p>
<label for=fit>Framing</label>
<select id=fit name=fit>
<option value=0>Fill the screen (crop edges)</option>
<option value=1>Fit the whole photo (white borders)</option>
</select>
<label for=dither>Dithering</label>
<select id=dither name=dither>
<option value=0>None (flat, blocky)</option>
<option value=1>Ordered / Bayer 8&times;8</option>
<option value=2>Floyd-Steinberg (recommended)</option>
<option value=3>Jarvis (smoothest, slowest)</option>
<option value=4>Atkinson (high contrast)</option>
</select>
<label for=gamma>Brightness (gamma)</label>
<input id=gamma name=gamma type=number step=0.05 min=0.2 max=4>
<p class=hint>Above 1.0 brightens, below 1.0 darkens. Try 1.2 if photos look
muddy on the panel.</p>
<div class=chk><input id=showFooter name=showFooter type=checkbox><label for=showFooter>Show a caption bar along the bottom</label></div>
</div>

<div class=card>
<h2>Device</h2>
<label for=configWindowMinutes>Keep settings open for (minutes)</label>
<input id=configWindowMinutes name=configWindowMinutes type=number min=1 max=60>
<label for=lowBatteryPercent>Pause the slideshow below (% battery)</label>
<input id=lowBatteryPercent name=lowBatteryPercent type=number min=0 max=50>
<label for=cacheFrames>Rendered frames to keep on the SD card</label>
<input id=cacheFrames name=cacheFrames type=number min=0 max=200>
<p class=hint>Each one is 960&nbsp;kB and makes revisiting that photo instant.</p>
<label for=adminUser>Admin username (optional)</label>
<input id=adminUser name=adminUser autocomplete=off>
<label for=adminPass>Admin password</label>
<input id=adminPass name=adminPass type=password placeholder="leave blank to keep">
</div>

<div class=card>
<div class=btns>
<button class=p type=submit>Save</button>
<button type=button onclick="act('playlist')">Re-read album</button>
<button type=button onclick="act('cache')">Clear cache</button>
<button type=button onclick="act('exit')">Save &amp; resume slideshow</button>
<button type=button onclick="act('reboot')">Save &amp; reconnect</button>
</div>
<div class=msg id=f_msg></div>
</div>
</form>
<p class=note>Hold the Refresh key on the front for 2 seconds to reopen this page.</p>
</div>

<script>
const $=i=>document.getElementById(i);
const FIELDS=['wifiSsid','immichUrl','albumId','intervalMinutes','imageSize',
  'playlistTtlHours','rotation','fit','dither','gamma','configWindowMinutes',
  'lowBatteryPercent','cacheFrames','adminUser'];
const SECRETS=['wifiPass','immichKey','adminPass'];
function msg(el,text,ok){el.textContent=text;el.className='msg '+(ok?'ok':'bad');}
function clr(el){el.className='msg';el.textContent='';}

async function load(){
  const c=await (await fetch('/api/config')).json();
  FIELDS.forEach(k=>{if(c[k]!==undefined)$(k).value=c[k];});
  $('shuffle').checked=!!c.shuffle;
  $('showFooter').checked=!!c.showFooter;
  if(c.albumId&&c.albumName)
    $('albumSel').innerHTML='<option value="'+c.albumId+'" data-n="'+
      c.albumName.replace(/"/g,'&quot;')+'">'+c.albumName+'</option>';
}

function render(s){
  $('s_pos').textContent=s.photos?(s.position+' / '+s.photos):'no playlist yet';
  $('s_bat').textContent=s.batteryPct+'%  ('+s.batteryV.toFixed(2)+' V)';
  $('s_sd').textContent=s.sdOk?'ready':'not mounted';
  $('s_cache').textContent=s.cacheFrames;
  $('s_render').textContent=s.renderMs?(s.renderMs+' ms, '+s.rotation+'\u00b0'):'-';
  $('s_window').textContent=s.windowLeft>0?(Math.ceil(s.windowLeft/60)+' min'):'-';
  if(s.busy)msg($('s_err'),s.busy+'\u2026',true);
  else if(s.lastError)msg($('s_err'),s.lastError,false);
  else clr($('s_err'));
}

async function status(){
  try{render(await (await fetch('/api/status')).json());}catch(e){}
}

// The device does the slow work on its main task, so every button is
// "post the intent, then wait for it to finish". A panel refresh takes about
// 40 s, hence the generous ceiling.
async function post(action,extra){
  const r=await fetch('/api/action',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(Object.assign({action:action},extra||{}))});
  if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));
}
async function waitIdle(maxMs){
  const t0=Date.now();
  for(;;){
    await new Promise(r=>setTimeout(r,900));
    let s;
    try{s=await (await fetch('/api/status')).json();}catch(e){continue;}
    render(s);
    if(!s.busy&&!s.queued)return s;
    if(Date.now()-t0>maxMs)return s;
  }
}

async function save(){
  const body={};
  FIELDS.forEach(k=>body[k]=$(k).value);
  body.shuffle=$('shuffle').checked;
  body.showFooter=$('showFooter').checked;
  // Carry the album's display name so the caption bar and status can show
  // something friendlier than a UUID.
  const sel=$('albumSel').selectedOptions[0];
  if(sel&&sel.value===body.albumId&&sel.dataset.n)body.albumName=sel.dataset.n;
  // Blank secrets mean "keep what is stored", so only send them when filled in.
  SECRETS.forEach(k=>{if($(k).value)body[k]=$(k).value;});
  const r=await fetch('/api/config',{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));
  SECRETS.forEach(k=>$(k).value='');
}

async function onSave(ev){
  ev.preventDefault();
  try{await save();msg($('f_msg'),'Saved.',true);}
  catch(e){msg($('f_msg'),'Save failed: '+e.message,false);}
  return false;
}

async function test(){
  msg($('i_msg'),'Testing\u2026',true);
  try{
    await save();                 // test what is on screen, not what was stored
    await post('test');
    const s=await waitIdle(60000);
    msg($('i_msg'),s.testMsg||'No answer from the device.',!!s.testOk);
  }catch(e){msg($('i_msg'),e.message,false);}
}

async function albums(){
  msg($('i_msg'),'Loading albums\u2026',true);
  try{
    await save();
    await post('albums');
    const s=await waitIdle(60000);
    if(s.albumsErr){msg($('i_msg'),s.albumsErr,false);return;}
    const a=await (await fetch('/api/albums')).json();
    $('albumSel').innerHTML='<option value="">\u2014 pick an album \u2014</option>'+
      a.map(x=>'<option value="'+x.id+'" data-n="'+x.name.replace(/"/g,'&quot;')+'">'+
        x.name+' ('+x.count+')</option>').join('');
    if($('albumId').value)$('albumSel').value=$('albumId').value;
    msg($('i_msg'),a.length+' albums found.',true);
  }catch(e){msg($('i_msg'),e.message,false);}
}

function pickAlbum(){
  const o=$('albumSel').selectedOptions[0];
  if(o&&o.value)$('albumId').value=o.value;
}

async function scan(){
  try{
    await save();
    await post('scan');
    await waitIdle(30000);
    const n=await (await fetch('/api/wifi/scan')).json();
    n.sort((a,b)=>b.rssi-a.rssi);
    $('ssids').innerHTML=n.map(x=>
      '<option value="'+x.ssid.replace(/"/g,'&quot;')+'">').join('');
  }catch(e){msg($('f_msg'),e.message,false);}
}

async function act(a){
  clr($('f_msg'));
  try{
    if(a!=='cache')await save();
    await post(a);
    // A photo change includes a ~40 s panel refresh.
    await waitIdle(a==='cache'?20000:120000);
  }catch(e){msg($('f_msg'),e.message,false);}
}

$('f').addEventListener('submit',onSave);
load();status();setInterval(status,3000);
</script>
)HTMLPAGE";
