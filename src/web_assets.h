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
.pal{display:grid;grid-template-columns:70px 1fr 36px;gap:6px 8px;align-items:center;margin-top:4px}
.pal label{margin:0;font-size:13px}
.sw{height:30px;border-radius:6px;border:1px solid var(--line)}
.grp{border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-top:12px}
.grp h3{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--mute);margin:0 0 4px}
.sl{display:flex;align-items:center;gap:10px}
.sl input[type=range]{flex:1 1 auto;width:auto;padding:0;background:none;border:0}
.sl output{flex:0 0 48px;text-align:right;font-variant-numeric:tabular-nums;font-size:13px;color:var(--mute)}
details summary{cursor:pointer;font-size:13px;color:var(--mute);margin-top:12px}
[hidden]{display:none!important}
#pvwrap{text-align:center;margin-bottom:4px}
#pv{max-width:100%;border:1px solid var(--line);border-radius:8px;background:var(--bg)}
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
<option value=1>Fit the whole photo (borders in the palette's white)</option>
</select>
<label for=gamma>Brightness (gamma)</label>
<input id=gamma name=gamma type=number step=0.05 min=0.2 max=4>
<p class=hint>Above 1.0 brightens, below 1.0 darkens. This is the oldest knob
here and it overlaps <b>Exposure</b> below; leave it at 1.0 unless you were
already using it.</p>
<div class=chk><input id=showFooter name=showFooter type=checkbox><label for=showFooter>Show a caption bar along the bottom</label></div>
</div>

<div class=card>
<h2>Palette</h2>
<p class=hint>The panel's six inks are nowhere near the saturated RGB they are
usually described with &mdash; measured, its white is a light grey around
<code>#B9C7C9</code> and its green is very dark. Dithering against the real
colours instead of the ideal ones is what stops photos looking washed out,
because both the colour chosen and the error passed to the neighbouring pixels
are then computed against what the panel will actually show.</p>
<label for=paletteId>Calibration</label>
<select id=paletteId name=paletteId onchange=sync()>
<option value=0>Theoretical (saturated) &mdash; the original look</option>
<option value=1>Spectra 6 measured (recommended)</option>
<option value=2>Spectra 6 measured, legacy</option>
<option value=3>Spectra 6 measured, Boeber</option>
<option value=4>Spectra 6 measured, aitjcize</option>
<option value=5>Custom &mdash; my own measurements</option>
</select>
<div id=palCustomBox hidden>
<div class=pal id=palGrid></div>
<div class=btns>
<button type=button onclick=palCopy()>Copy the selected built-in into these</button>
<button type=button onclick="act('chart')">Draw the test chart</button>
</div>
<p class=hint>To measure your own panel: press <b>Draw the test chart</b> (one
full refresh, about 40&nbsp;s), photograph the screen in even, indirect light
with no flash, then read each band with any colour picker and type the values
in. Bands run top to bottom in the order listed above.</p>
</div>
</div>

<div class=card>
<h2>Dithering</h2>
<label for=ditherType>Method</label>
<select id=ditherType name=ditherType onchange=sync()>
<option value=0>Error diffusion (recommended)</option>
<option value=1>Ordered / Bayer</option>
<option value=2>Random noise</option>
<option value=3>None &mdash; nearest colour only</option>
</select>
<div id=edBox>
<label for=edMatrix>Kernel</label>
<select id=edMatrix name=edMatrix>
<option value=0>Floyd&ndash;Steinberg (recommended)</option>
<option value=1>False Floyd&ndash;Steinberg (faster, coarser)</option>
<option value=2>Atkinson (high contrast, drops 1/4 of the error)</option>
<option value=3>Jarvis&ndash;Judice&ndash;Ninke (smoothest, slowest)</option>
<option value=4>Stucki</option>
<option value=5>Burkes</option>
<option value=6>Sierra-3</option>
<option value=7>Sierra-2</option>
<option value=8>Sierra-2-4A (fastest of the conserving kernels)</option>
<option value=9>Fan</option>
<option value=10>Shiau&ndash;Fan</option>
<option value=11>Shiau&ndash;Fan 2</option>
</select>
<div class=chk><input id=serpentine name=serpentine type=checkbox><label for=serpentine>Serpentine &mdash; alternate the scan direction each row</label></div>
<p class=hint>Serpentine stops the error all drifting one way, which removes the
faint diagonal grain error diffusion can leave in flat areas.</p>
</div>
<div id=ordBox hidden>
<label for=bayerSize>Matrix size</label>
<select id=bayerSize name=bayerSize>
<option value=2>2&times;2</option>
<option value=4>4&times;4</option>
<option value=8>8&times;8</option>
<option value=16>16&times;16</option>
</select>
<label for=orderedStrength>Threshold spread</label>
<div class=sl><input id=orderedStrength name=orderedStrength type=range min=1 max=255 step=1><output for=orderedStrength></output></div>
<p class=hint>Ordered and random dithering have no error feedback, so they
cannot mix two inks to make a colour that is between them &mdash; a flat
mid-grey comes out as one flat ink. Use error diffusion for photographs.</p>
</div>
<label for=colorMatch>Colour distance</label>
<select id=colorMatch name=colorMatch>
<option value=0>RGB &mdash; plain Euclidean (recommended)</option>
<option value=1>LAB &mdash; perceptual (&Delta;E), slower</option>
<option value=2>Chroma &mdash; experimental, keeps pastels off white</option>
</select>
<p class=hint>How "nearest ink" is decided. LAB matches how the eye judges
difference; chroma penalises neutral inks for saturated source pixels, which
keeps pale pinks and blues from collapsing to white.</p>
</div>

<div class=card>
<h2>Image processing</h2>
<div id=pvwrap><canvas id=pv width=300 height=400></canvas></div>
<div class=btns>
<button type=button class=p onclick=preview()>Save &amp; preview</button>
</div>
<p class=hint id=pvhint>The preview renders the current photo at full size and
then shrinks it, so it shows the real colour and tone decisions &mdash; roughly
what the panel looks like from a step back. It costs a decode and a render (a
few seconds), not a 40-second refresh.</p>
<div class=msg id=p_msg></div>

<label for=procPreset>Preset</label>
<select id=procPreset name=procPreset onchange=onPreset()>
<option value=0>None &mdash; every stage neutral (default)</option>
<option value=1>Balanced &mdash; fit the display's brightness range</option>
<option value=2>Dynamic &mdash; brighter, punchier photos</option>
<option value=3>Vivid &mdash; boosted colour for illustrations</option>
<option value=4>Soft &mdash; lower contrast, smoother gradients</option>
<option value=5>Grayscale &mdash; monochrome, LAB matching</option>
<option value=6>Restore &mdash; faded scans and paintings</option>
<option value=7>Poster scan &mdash; neutralise warm paper</option>
</select>
<p class=hint>Picking a preset fills in everything below. Changing any one of
them switches this back to <b>None</b> and keeps your values.</p>

<div class=grp>
<h3>Tone</h3>
<label for=exposure>Exposure</label>
<div class=sl><input id=exposure name=exposure type=range min=0.5 max=2 step=0.01><output for=exposure></output></div>
<label for=saturation>Saturation</label>
<div class=sl><input id=saturation name=saturation type=range min=0 max=2 step=0.01><output for=saturation></output></div>
<label for=toneMode>Tone curve</label>
<select id=toneMode name=toneMode onchange=sync()>
<option value=0>Off</option>
<option value=1>Contrast</option>
<option value=2>S-curve</option>
</select>
<div id=contrastBox hidden>
<label for=contrast>Contrast</label>
<div class=sl><input id=contrast name=contrast type=range min=0.5 max=2 step=0.01><output for=contrast></output></div>
</div>
<div id=scBox hidden>
<label for=scStrength>S-curve strength</label>
<div class=sl><input id=scStrength name=scStrength type=range min=0 max=1 step=0.01><output for=scStrength></output></div>
<label for=scShadow>Shadow boost</label>
<div class=sl><input id=scShadow name=scShadow type=range min=0 max=1 step=0.01><output for=scShadow></output></div>
<label for=scHighlight>Highlight compression</label>
<div class=sl><input id=scHighlight name=scHighlight type=range min=0.5 max=5 step=0.01><output for=scHighlight></output></div>
<label for=scMidpoint>Midpoint</label>
<div class=sl><input id=scMidpoint name=scMidpoint type=range min=0.3 max=0.7 step=0.01><output for=scMidpoint></output></div>
</div>
</div>

<div class=grp>
<h3>Dynamic range</h3>
<label for=drcMode>Compress into the panel's range</label>
<select id=drcMode name=drcMode onchange=sync()>
<option value=0>Off</option>
<option value=1>Display &mdash; map 0&ndash;100% onto the palette</option>
<option value=2>Auto &mdash; measure the photo first</option>
</select>
<p class=hint>The panel's white reflects a little over half as much light as
paper white, so a photo's full range cannot fit. This squeezes it in instead of
letting the ends clip, and holds saturated colours back so they are not
flattened with the neutrals.</p>
<div id=drcBox hidden>
<label for=drcStrength>Strength</label>
<div class=sl><input id=drcStrength name=drcStrength type=range min=0 max=1 step=0.01><output for=drcStrength></output></div>
<label for=drcQuality>Lightness model</label>
<select id=drcQuality name=drcQuality>
<option value=0>Fast &mdash; linear luminance</option>
<option value=1>Accurate &mdash; CIE L* (slower)</option>
</select>
<div id=drcAutoBox hidden>
<label for=drcLowPct>Ignore the darkest</label>
<div class=sl><input id=drcLowPct name=drcLowPct type=range min=0 max=0.2 step=0.005><output for=drcLowPct></output></div>
<label for=drcHighPct>Ignore above</label>
<div class=sl><input id=drcHighPct name=drcHighPct type=range min=0.8 max=1 step=0.005><output for=drcHighPct></output></div>
</div>
<div class=chk><input id=drcPreserveWhite name=drcPreserveWhite type=checkbox><label for=drcPreserveWhite>Keep background white white (for scans)</label></div>
</div>
</div>

<div class=grp>
<h3>Clarity</h3>
<label for=clarityAmount>Local midtone contrast</label>
<div class=sl><input id=clarityAmount name=clarityAmount type=range min=-1 max=1 step=0.01><output for=clarityAmount></output></div>
<p class=hint>Positive sharpens local detail, negative softens it. The most
expensive stage here: it is the only one that has to look at a pixel's
neighbours, so it adds a second pass over the photo.</p>
<div id=clarBox hidden>
<label for=clarityRadius>Radius</label>
<div class=sl><input id=clarityRadius name=clarityRadius type=range min=1 max=4 step=1><output for=clarityRadius></output></div>
<label for=clarityMidtone>Midtone focus</label>
<div class=sl><input id=clarityMidtone name=clarityMidtone type=range min=0.1 max=4 step=0.05><output for=clarityMidtone></output></div>
</div>
</div>

<div class=grp>
<h3>Paper</h3>
<label for=paperMode>Scan cleanup</label>
<select id=paperMode name=paperMode onchange=sync()>
<option value=0>Off</option>
<option value=1>Neutralise warm paper</option>
</select>
<div id=paperBox hidden>
<label for=paperStrength>Strength</label>
<div class=sl><input id=paperStrength name=paperStrength type=range min=0 max=1 step=0.01><output for=paperStrength></output></div>
</div>
<p class=hint>For photographed or scanned documents and posters: takes the
yellow cast off aged paper, pulls dark neutral ink towards black, and leaves
saturated red ink alone.</p>
</div>

<details>
<summary>Advanced</summary>
<div class=grp>
<h3>Level remap (legacy)</h3>
<label for=levelMode>Mode</label>
<select id=levelMode name=levelMode onchange=sync()>
<option value=0>Off</option>
<option value=1>Per channel</option>
<option value=2>Luminance</option>
</select>
<div id=lvlBox hidden>
<div class=chk><input id=levelAuto name=levelAuto type=checkbox><label for=levelAuto>Only when the photo actually exceeds the range</label></div>
</div>
<p class=hint>An older way of fitting the photo into the palette's range that
<b>Dynamic range</b> above supersedes. Per-channel remapping squeezes colour
along with brightness and visibly washes out midtones &mdash; the C reference
firmware tried it and reverted it. Left here for completeness; leave it off.</p>
</div>
</details>
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
  'playlistTtlHours','rotation','fit','gamma','configWindowMinutes',
  'lowBatteryPercent','cacheFrames','adminUser',
  'paletteId','ditherType','edMatrix','colorMatch','bayerSize','orderedStrength',
  'procPreset','exposure','saturation','toneMode','contrast','scStrength',
  'scShadow','scHighlight','scMidpoint','drcMode','drcStrength','drcLowPct',
  'drcHighPct','drcQuality','levelMode','clarityAmount','clarityRadius',
  'clarityMidtone','paperMode','paperStrength'];
const CHECKS=['shuffle','showFooter','serpentine','drcPreserveWhite','levelAuto'];
const SECRETS=['wifiPass','immichKey','adminPass'];
// The six palette slots, in the order e6_dither.cpp indexes them.
const SLOTS=['White','Green','Red','Yellow','Blue','Black'];
let BUILTINS={};   // from /api/config, so the hex values live in one place only
let PALIDS=['seeed','spectra6','spectra6legacy','spectra6boeber','aitjcize','custom'];
function msg(el,text,ok){el.textContent=text;el.className='msg '+(ok?'ok':'bad');}
function clr(el){el.className='msg';el.textContent='';}

// The six custom-palette rows are built here rather than in the markup so the
// slot names and the order live in exactly one place.
function palBuild(){
  $('palGrid').innerHTML=SLOTS.map((n,i)=>
    '<label for=pal'+i+'>'+n+'</label>'+
    '<input id=pal'+i+' maxlength=7 spellcheck=false autocomplete=off>'+
    '<div class=sw id=sw'+i+'></div>').join('');
  for(let i=0;i<6;i++)$('pal'+i).addEventListener('input',()=>palSwatch(i));
}
function palSwatch(i){
  const v=$('pal'+i).value.trim();
  // Only paint a swatch for something the firmware would also accept; a
  // half-typed value should not flash a misleading colour.
  $('sw'+i).style.background=/^#?([0-9a-f]{3}|[0-9a-f]{6})$/i.test(v)
    ?(v[0]==='#'?v:'#'+v):'transparent';
}
function palSet(a){ for(let i=0;i<6;i++){$('pal'+i).value=a[i];palSwatch(i);} }
function palCopy(){
  // Seed the custom fields from whichever built-in is nearest to hand: the one
  // selected, or the recommended measured palette when "Custom" is selected.
  const id=+$('paletteId').value;
  const name=PALIDS[id===5?1:id];
  if(BUILTINS[name])palSet(BUILTINS[name]);
}
function rngWire(){
  document.querySelectorAll('.sl input[type=range]').forEach(r=>{
    const o=document.querySelector('output[for="'+r.id+'"]');
    const upd=()=>{o.textContent=(+r.value).toFixed(+r.step>=1?0:2);};
    r.addEventListener('input',upd);
    upd();
  });
}
function rngRefresh(){
  document.querySelectorAll('.sl input[type=range]').forEach(r=>{
    const o=document.querySelector('output[for="'+r.id+'"]');
    o.textContent=(+r.value).toFixed(+r.step>=1?0:2);
  });
}

// Show only the controls that the current modes actually use.
function sync(){
  const dt=+$('ditherType').value;
  $('edBox').hidden=dt!==0;
  $('ordBox').hidden=!(dt===1||dt===2);
  $('palCustomBox').hidden=+$('paletteId').value!==5;
  const tm=+$('toneMode').value;
  $('contrastBox').hidden=tm!==1;
  $('scBox').hidden=tm!==2;
  const dm=+$('drcMode').value;
  $('drcBox').hidden=dm===0;
  $('drcAutoBox').hidden=dm!==2;
  $('clarBox').hidden=Math.abs(+$('clarityAmount').value)<0.005;
  $('paperBox').hidden=+$('paperMode').value===0;
  $('lvlBox').hidden=+$('levelMode').value===0;
}

async function load(){
  const c=await (await fetch('/api/config')).json();
  FIELDS.forEach(k=>{if(c[k]!==undefined)$(k).value=c[k];});
  CHECKS.forEach(k=>{$(k).checked=!!c[k];});
  BUILTINS=c.paletteBuiltins||{};
  if(c.paletteCustom)palSet(c.paletteCustom);
  rngRefresh();
  sync();
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

async function save(extra){
  const body={};
  FIELDS.forEach(k=>body[k]=$(k).value);
  CHECKS.forEach(k=>body[k]=$(k).checked);
  body.paletteCustom=[0,1,2,3,4,5].map(i=>$('pal'+i).value.trim());
  if(extra)Object.assign(body,extra);
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

// Presets live in the firmware (imgprocPreset() in src/imgproc.cpp), so picking
// one here posts the choice, lets the device fill the fields in, and reloads --
// rather than keeping a second copy of the preset table in this page that could
// drift out of step with it.
async function onPreset(){
  clr($('p_msg'));
  if(+$('procPreset').value===0){sync();return;}
  try{
    await save({applyPreset:true});
    await load();
    msg($('p_msg'),'Preset applied. Press "Save & preview" to see it.',true);
  }catch(e){msg($('p_msg'),e.message,false);}
}

// Any manual edit means the settings are no longer that preset.
function unpreset(e){
  const t=e.target;
  if(!t.id||t.id==='procPreset')return;
  if(!/^(exposure|saturation|toneMode|contrast|sc|drc|clarity|paper|levelMode|levelAuto)/.test(t.id))return;
  $('procPreset').value=0;
}

// ---- preview ---------------------------------------------------------------
// The device serves raw RGB888 at panel-native orientation. Rotating it back
// here is pixel-by-pixel rather than a canvas transform, because the mapping is
// the exact inverse of emitRow() in src/render.cpp and worth being able to read
// off against it.
function pvDraw(raw,pw,ph,rot){
  let dw=pw,dh=ph;
  if(rot===90||rot===270){dw=ph;dh=pw;}
  const cv=$('pv');
  cv.width=dw;cv.height=dh;
  const ctx=cv.getContext('2d');
  const img=ctx.createImageData(dw,dh);
  for(let dy=0;dy<dh;dy++)for(let dx=0;dx<dw;dx++){
    let sx,sy;
    if(rot===270){sx=dy;sy=dw-1-dx;}
    else if(rot===90){sx=dh-1-dy;sy=dx;}
    else if(rot===180){sx=pw-1-dx;sy=ph-1-dy;}
    else{sx=dx;sy=dy;}
    const si=(sy*pw+sx)*3,di=(dy*dw+dx)*4;
    img.data[di]=raw[si];img.data[di+1]=raw[si+1];img.data[di+2]=raw[si+2];img.data[di+3]=255;
  }
  ctx.putImageData(img,0,0);
}

async function preview(){
  msg($('p_msg'),'Rendering\u2026',true);
  try{
    await save();                 // preview what is on screen, not what was stored
    await post('preview');
    const s=await waitIdle(180000);
    if(!s.previewReady){
      msg($('p_msg'),s.previewErr||'The preview did not finish.',false);
      return;
    }
    const r=await fetch('/api/preview.bin',{cache:'no-store'});
    if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));
    const raw=new Uint8Array(await r.arrayBuffer());
    const need=s.previewW*s.previewH*3;
    if(raw.length<need)throw new Error('short preview ('+raw.length+' of '+need+' bytes)');
    pvDraw(raw,s.previewW,s.previewH,s.previewRot);
    msg($('p_msg'),'Preview rendered in '+s.renderMs+' ms.',true);
  }catch(e){msg($('p_msg'),e.message,false);}
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
$('f').addEventListener('input',e=>{unpreset(e);sync();});
palBuild();
rngWire();
load();status();setInterval(status,3000);
</script>
)HTMLPAGE";
