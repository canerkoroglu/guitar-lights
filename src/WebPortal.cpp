#include "WebPortal.h"
#include <WebServer.h>
#include "Config.h"
#include "AudioDsp.h"
#include "GuitarSynth.h"
#include "AudioInput.h"
#include "TestTone.h"
#include "Presets.h"
#include "EffectCatalog.h"
#include "Stage.h"
#include "Battery.h"
#include "FirmwareVersion.h"
#include <ArduinoJson.h>

static WebServer   server(80);
static CRGB*       ledBuf   = nullptr;
static uint16_t    ledCount = 0;
static constexpr uint16_t MAX_DEBUG_LEDS = 64;
static CRGB        debugColors[MAX_DEBUG_LEDS];
static bool        debugOn[MAX_DEBUG_LEDS] = {};
static bool        debugActive = false;

static const char INDEX_HTML[] PROGMEM = R"rawhtml(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Guitar RGB</title>
<style>
:root{--bg:#0e0f13;--card:#1a1d24;--line:#2a2f3a;--fg:#e7e9ee;--mut:#8a90a0;--acc:#5b8cff}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0 auto;max-width:560px;padding:16px;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,sans-serif}
h1{font-size:20px;margin:0 0 2px}
.sub{color:var(--mut);margin:0 0 16px;font-size:13px}
.card{background:var(--card);border-radius:12px;padding:6px 16px;margin-bottom:14px}
.card h2{font-size:13px;font-weight:600;color:var(--mut);text-transform:uppercase;letter-spacing:.04em;margin:12px 0 4px}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:44px}
.row>span{color:var(--mut)}
input[type=range]{flex:1;max-width:60%;accent-color:var(--acc)}
select{font:inherit;background:#11141b;color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:8px}
input[type=color]{width:44px;height:32px;border:1px solid var(--line);border-radius:6px;background:none;padding:2px}
.notes{display:grid;grid-template-columns:repeat(6,1fr);gap:10px;padding:8px 0 14px}
.note{display:flex;flex-direction:column;align-items:center;gap:4px;font-size:12px;color:var(--mut)}
.note input{width:100%}
.strip{display:flex;gap:4px;justify-content:space-between;padding:6px 0 14px}
.strip b{flex:1;max-width:22px;aspect-ratio:1;border-radius:50%;background:#000;border:1px solid var(--line)}
.btns{display:flex;gap:10px}
button{flex:1;font:inherit;border:0;border-radius:10px;padding:12px;cursor:pointer;background:var(--acc);color:#fff}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--fg)}
.row button{flex:none;padding:8px 18px}
.hint{font-size:12px;color:var(--mut);margin:0 0 12px}
#desc{min-height:2.8em}
.sw{position:relative;width:46px;height:26px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw i{position:absolute;inset:0;background:var(--line);border-radius:13px;transition:.2s}
.sw i:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+i{background:var(--acc)}
.sw input:checked+i:before{transform:translateX(20px)}
details.card>summary{display:flex;justify-content:space-between;align-items:center;cursor:pointer;list-style:none}
details.card>summary::-webkit-details-marker{display:none}
details.card>summary::after{content:"\25B8";color:var(--mut)}
details.card[open]>summary::after{content:"\25BE"}
canvas{display:block;width:100%;height:140px;background:#0b0c10;border-radius:8px;margin:6px 0}
.chip{font-size:12px;font-weight:600;padding:3px 10px;border-radius:999px;background:var(--line)}
.chip.ok{background:#1f5f3a}.chip.bad{background:#7a2530}.chip.info{background:#2c3f7a}
.meter{flex:1;position:relative;height:12px;background:#0b0c10;border-radius:6px;overflow:hidden}
.meter i{position:absolute;left:0;top:0;bottom:0;width:0;background:linear-gradient(90deg,#1f9d55,#e3b341 70%,#d1453b);transition:width .1s}
.dot{width:12px;height:12px;border-radius:50%;background:var(--line);flex:none;transition:background .15s}
.dot.on{background:#fff;box-shadow:0 0 10px #fff}
.row small{color:var(--fg);margin-left:8px}
.row.dim{opacity:.35}
.presets{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;padding:6px 0 14px}
.presets button{padding:10px 4px;font-size:13px;background:#232836;border:1px solid var(--line)}
.presets button.on{background:var(--acc);border-color:var(--acc)}
.palette-edit{border-top:1px solid var(--line);margin-top:8px;padding-top:10px}
.palette-edit button{padding:9px;font-size:13px}
.ledtest{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px;padding:6px 0 10px}
.ledcell{display:flex;min-width:0;align-items:center;justify-content:space-between;gap:2px;padding:4px;background:#11141b;border:1px solid var(--line);border-radius:7px;font-size:10px}
.ledcell>span{white-space:nowrap}
.ledcell label{display:flex;align-items:center;gap:2px;font-size:9px}
.ledcell input[type=checkbox]{width:12px;height:12px;margin:0}
.ledcell input[type=color]{width:24px;height:23px;padding:1px;flex:none}
.ledtest-actions{display:flex;gap:8px;padding-bottom:10px}
.ledtest-actions button{padding:9px 5px;font-size:13px}
.tuner{display:flex;flex-direction:column;align-items:center;padding:4px 0 14px}
.tnote{font-size:44px;font-weight:700;line-height:1.1;min-height:1.1em}
.tcents{font-size:14px;color:var(--mut);min-height:1.4em}
.tscale{position:relative;width:100%;height:18px;margin-top:6px;border-radius:9px;background:linear-gradient(90deg,#7a2530,#8a6a1a 35%,#1f5f3a 47%,#1f5f3a 53%,#8a6a1a 65%,#7a2530)}
.tscale b{position:absolute;top:-3px;width:4px;height:24px;margin-left:-2px;border-radius:2px;background:#fff;left:50%;transition:left .12s;display:none}
.tnote.in{color:#3ddc84}
.chip.warn{background:#8a6a1a}
@media (max-width:420px){.notes{grid-template-columns:repeat(4,1fr)}}
</style></head><body>
<h1>Guitar RGB</h1>
<p class="sub"><span id="ledCount">10</span> &times; WS2812B &middot; <span id="stat">connecting&hellip;</span><span id="batHead"></span></p>
<p class="sub"><span class="chip" id="fwVersion">Checking firmware&hellip;</span></p>

<div class="card">
  <h2>Live</h2>
  <div class="strip" id="strip"></div>
</div>

<details class="card" id="ledTestCard">
  <summary><h2>Individual LED test</h2></summary>
  <p class="hint">Select each LED's color and switch it on or off. Test mode uses full brightness and temporarily pauses effects.</p>
  <div class="ledtest" id="ledTestGrid"></div>
  <div class="ledtest-actions">
    <button class="ghost" id="ledAllOff">All off</button>
    <button class="ghost" id="ledAllOn">All on</button>
    <button class="ghost" id="ledChase">Chase</button>
    <button id="ledTestExit">Exit test</button>
  </div>
  <p class="hint" id="ledTestStatus">Test mode is off.</p>
</details>

<div class="card">
  <h2>Tuner</h2>
  <div class="row"><span>Tuner on the LEDs</span><label class="sw"><input type="checkbox" id="tunerOn" data-local><i></i></label></div>
  <div class="tuner" id="tunerView" hidden>
    <div class="tnote" id="tNote">&ndash;</div>
    <div class="tcents" id="tCents">play a single note</div>
    <div class="tscale"><b id="tNeedle"></b></div>
  </div>
  <p class="hint">Also: long press the mode button. In tune = the two middle LEDs turn green.</p>
</div>

<div class="card">
  <h2>Presets</h2>
  <div class="presets" id="presets"></div>
  <div class="palette-edit">
    <p class="hint">Choose a preset, edit its 12 note colors, then save. The palette stays with that preset.</p>
    <div class="row"><span>Preset palette</span><select id="palettePreset"></select></div>
    <div class="notes" id="presetPalette"></div>
    <button class="ghost" id="savePresetPalette">Save preset palette</button>
    <span class="hint" id="paletteStatus"></span>
  </div>
</div>

<details class="card" id="inputCard">
  <summary><h2>Guitar input (GPIO34)</h2></summary>
  <canvas id="scope"></canvas>
  <p class="hint" id="adcStat">Waiting for samples&hellip;</p>
  <div class="row"><span>Status</span><b class="chip" id="adcState">&ndash;</b></div>
  <div class="row"><span>Noise / gate margin</span><b id="noiseMargin">&ndash;</b></div>
  <div class="row"><span>Quiet calibration</span><button id="calibrate">Measure</button></div>
  <p class="hint" id="calStat">Keep the guitar quiet for 3 seconds. The suggested gate is 15 dB above the measured noise floor.</p>
  <div class="row"><span>Zoom to signal</span><label class="sw"><input type="checkbox" id="zoom" data-local checked><i></i></label></div>
  <div class="row"><span>Test tone on GPIO26</span><label class="sw"><input type="checkbox" id="tone" data-local><i></i></label></div>
  <p class="hint">Loopback test: jumper GPIO26 to GPIO34 with nothing else on GPIO34. Remove the jumper before wiring the buffer circuit.</p>
</details>

<div class="card">
  <h2>Output</h2>
  <div class="row"><span>Power</span><label class="sw"><input type="checkbox" id="power"><i></i></label></div>
  <div class="row"><span>Brightness<small id="brightnessV"></small></span><input type="range" id="brightness" min="0" max="255"></div>
  <div class="row"><span>Effect</span>
    <select id="mode"></select></div>
  <p class="hint" id="desc"></p>
  <div class="row"><span>Speed<small id="speedV"></small></span><input type="range" id="speed" min="1" max="100"></div>
  <div class="row"><span>Trail<small id="trailV"></small></span><input type="range" id="trail" min="1" max="100"></div>
  <div class="row"><span>Static color</span><input type="color" id="staticColor"></div>
</div>

<div class="card">
  <h2>Note &rarr; color</h2>
  <p class="hint">One color per note name, any octave. Used by every audio effect.</p>
  <div class="notes" id="notes"></div>
</div>

<div class="card">
  <h2>Pitch &rarr; position</h2>
  <p class="hint">Which notes land at each end of the strip. Used by Pitch position and Ripple.</p>
  <div class="row"><span>Lowest note</span><select id="pitchLow"></select></div>
  <div class="row"><span>Highest note</span><select id="pitchHigh"></select></div>
  <div class="row"><span>Reverse direction</span><label class="sw"><input type="checkbox" id="reverse"><i></i></label></div>
</div>

<div class="card">
  <h2>Audio</h2>
  <div class="row"><span>Source</span>
    <select id="source">
      <option value="0">Simulated guitar</option>
      <option value="1">Guitar input (GPIO34)</option>
    </select></div>
  <div class="row"><span>Level</span><div class="meter"><i id="meter"></i></div><b class="dot" id="attackDot" title="attack"></b></div>
  <div class="row"><span>Pitch</span><b id="pitchNow">&ndash;</b></div>
  <p class="hint" id="audioStat">&ndash;</p>
  <div class="row"><span>Noise gate<small id="gateDbV"></small></span><input type="range" id="gateDb" min="-80" max="-10"></div>
  <div class="row"><span>Full scale<small id="fullDbV"></small></span><input type="range" id="fullDb" min="-40" max="0"></div>
  <div class="row"><span>Release<small id="releaseMsV"></small></span><input type="range" id="releaseMs" min="20" max="1000" step="10"></div>
  <div class="row"><span>Attack threshold<small id="attackDbV"></small></span><input type="range" id="attackDb" min="2" max="30"></div>
  <div class="row"><span>Pitch confidence<small id="pitchConfV"></small></span><input type="range" id="pitchConf" min="50" max="99"></div>
  <p class="hint">Noise gate: anything quieter counts as silence. Full scale: this loud lights every LED. Release: how fast the level falls. Attack threshold: how sharp a jump counts as a pick; lower is more sensitive. Pitch confidence: how clean a note must be before its pitch is used; lower accepts messier notes.</p>
</div>

<div class="card">
  <h2>Battery</h2>
  <div class="row"><span>Battery monitor fitted</span><label class="sw"><input type="checkbox" id="batteryFitted"><i></i></label></div>
  <div class="row batrow"><span>Charge</span><b class="chip" id="batState">&ndash;</b></div>
  <div class="row batrow"><span>Calibration<small id="batteryTrimV"></small></span><input type="range" id="batteryTrim" min="-300" max="300" step="10"></div>
  <div class="row batrow"><span>Show on LEDs</span><button id="batShow">Show</button></div>
  <p class="hint">Needs a 100k/100k divider from the cell to GPIO35 (readme &ldquo;Battery monitor&rdquo;). Calibration: measure the cell with a multimeter and move the slider until the reading matches. Hold the mode button 2 s to see the charge on the LEDs.</p>
</div>

<div class="card">
  <h2>Simulated guitar</h2>
  <p class="hint">Used when Source is Simulated guitar. Auto-play picks notes with varied strength, slides and rests. Turn it off to pick a note yourself.</p>
  <div class="row"><span>Auto-play</span><label class="sw"><input type="checkbox" id="testAuto"><i></i></label></div>
  <div class="row"><span>Pick strength<small id="testLevelV"></small></span><input type="range" id="testLevel" min="0" max="100"></div>
  <div class="row"><span>Note</span><select id="testNote"></select></div>
  <div class="row"><span>Pick</span><button id="pluck">Pluck</button></div>
</div>

<div class="btns">
  <button class="ghost" id="reset">Reset defaults</button>
  <button id="save">Apply now</button>
</div>

<script>
const NOTES=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
// Effect list from the firmware (/api/effects): {mode, group, name, desc, uses}.
// uses: which of Speed (s), Trail (t) and Static color (c) the effect reads.
let EFFECTS={};
const $=id=>document.getElementById(id);
const name=m=>NOTES[m%12]+(Math.floor(m/12)-1);
const range=(a,b)=>{let s="";for(let m=a;m<=b;m++)s+=`<option value="${m}">${name(m)}</option>`;return s;};
let timer=null, pending={};

$("strip").innerHTML="<b></b>".repeat(10);
$("notes").innerHTML=NOTES.map((n,i)=>`<label class="note"><input type="color" data-note="${i}">${n}</label>`).join("");
// Same limits as ConfigStore::fromJson: pitchLow 24..107, pitchHigh 25..108, test note 24..108.
const MIDI_MIN=24, MIDI_MAX=108;
$("pitchLow").innerHTML=range(MIDI_MIN,MIDI_MAX-1);
$("pitchHigh").innerHTML=range(MIDI_MIN+1,MIDI_MAX);
$("testNote").innerHTML='<option value="-1">none</option>'+range(MIDI_MIN,MIDI_MAX);

// Effect description, and dim the rows the effect ignores.
function desc(){
  const e=EFFECTS[+$("mode").value]||{}, u=e.uses||"";
  $("desc").textContent=e.desc||"";
  $("speed").closest(".row").classList.toggle("dim",!u.includes("s"));
  $("trail").closest(".row").classList.toggle("dim",!u.includes("t"));
  $("staticColor").closest(".row").classList.toggle("dim",!u.includes("c"));
}

function fill(c){
  $("power").checked=c.power;
  $("brightness").value=c.brightness;
  $("mode").value=c.mode;
  $("speed").value=c.speed;
  $("trail").value=c.trail;
  $("staticColor").value=c.staticColor;
  document.querySelectorAll("[data-note]").forEach(el=>el.value=c.noteColors[+el.dataset.note]);
  $("pitchLow").value=c.pitchLow;
  $("pitchHigh").value=c.pitchHigh;
  $("reverse").checked=c.reverse;
  $("source").value=c.source;
  $("gateDb").value=c.gateDb;
  $("fullDb").value=c.fullDb;
  $("releaseMs").value=c.releaseMs;
  $("attackDb").value=c.attackDb;
  $("pitchConf").value=c.pitchConf;
  $("testAuto").checked=c.test.auto;
  $("testLevel").value=Math.round(c.test.level*100);
  $("testNote").value=c.test.note;
  $("batteryFitted").checked=c.battery.fitted;
  $("batteryTrim").value=c.battery.trimMv;
  batRows();
  desc();
  labels();
}

function labels(){
  $("brightnessV").textContent=Math.round($("brightness").value/255*100)+" %";
  $("speedV").textContent=$("speed").value;
  $("trailV").textContent=$("trail").value;
  $("testLevelV").textContent=$("testLevel").value+" %";
  const tr=+$("batteryTrim").value;
  $("batteryTrimV").textContent=(tr>0?"+":"")+tr+" mV";
  $("gateDbV").textContent=$("gateDb").value+" dB";
  $("fullDbV").textContent=$("fullDb").value+" dB";
  $("releaseMsV").textContent=$("releaseMs").value+" ms";
  $("attackDbV").textContent=$("attackDb").value+" dB";
  $("pitchConfV").textContent=$("pitchConf").value+" %";
}

// Only the setting a control changed is sent (the firmware merges partial
// updates), so a second phone or a preset applied over serial is not
// overwritten by this page's older copy of everything else.
function patchFor(el){
  const id=el.id, v=el.type==="checkbox"?el.checked:el.value;
  if(el.dataset.note!==undefined){const nc=[];nc[+el.dataset.note]=el.value;return {noteColors:nc};}  // others null: unchanged
  switch(id){
    case "power":case "reverse":return {[id]:v};
    case "staticColor":return {staticColor:v};
    case "pitchLow":case "pitchHigh":return {pitchLow:+$("pitchLow").value,pitchHigh:+$("pitchHigh").value};
    case "gateDb":case "fullDb":return {gateDb:+$("gateDb").value,fullDb:+$("fullDb").value};
    case "testAuto":return {test:{auto:v}};
    case "testLevel":return {test:{level:+v/100}};
    case "testNote":return {test:{note:+v}};
    case "batteryFitted":return {battery:{fitted:v}};
    case "batteryTrim":return {battery:{trimMv:+v}};
    case "brightness":case "mode":case "speed":case "trail":case "source":
    case "releaseMs":case "attackDb":case "pitchConf":return {[id]:+v};
  }
  return null;
}

// Merge b into a: nested objects merge, sparse arrays keep a's entries where b has none.
function merge(a,b){
  for(const k in b){
    if(Array.isArray(b[k])){a[k]=a[k]||[];b[k].forEach((x,i)=>{if(x!==undefined)a[k][i]=x;});}
    else if(b[k]&&typeof b[k]==="object"){a[k]=merge(a[k]||{},b[k]);}
    else a[k]=b[k];
  }
  return a;
}

function status(t){$("stat").textContent=t;}

// Battery rows only make sense with the divider fitted.
function batRows(){document.querySelectorAll(".batrow").forEach(r=>r.hidden=!$("batteryFitted").checked);}

function showBattery(b){
  const c=$("batState");
  if(!b||!b.fitted){$("batHead").textContent="";c.textContent="–";c.className="chip";return;}
  if(b.state==="unknown"){$("batHead").textContent="";c.textContent="No reading yet";c.className="chip info";return;}
  $("batHead").textContent=` · battery ${b.pct} %`;
  c.textContent=`${b.pct} % · ${b.v.toFixed(2)} V`+(b.state==="ok"?"":b.state==="low"?" · low":" · critical")+
    (b.bootOnly?" · read at boot only":"");
  c.className="chip "+(b.state==="ok"?"ok":b.state==="low"?"warn":"bad");
}

// Tuner readout: note name, cents and a needle (half the scale = 50 cents).
// After the user flips a live switch, ignore the polled state for a moment:
// a reply already on its way would otherwise flip it back.
const heldUntil={};
const hold=id=>heldUntil[id]=Date.now()+800;
const held=id=>(heldUntil[id]||0)>Date.now();

function showTuner(d){
  if(!held("tunerOn"))$("tunerOn").checked=d.tuner;
  $("tunerView").hidden=!d.tuner;
  if(!d.tuner)return;
  const n=$("tNote"),c=$("tCents"),k=$("tNeedle");
  if(d.note<0){n.textContent="–";n.classList.remove("in");c.textContent="play a single note";k.style.display="none";return;}
  const ct=Math.max(-50,Math.min(50,d.cents)),inTune=Math.abs(d.cents)<=3;
  n.textContent=name(d.note);n.classList.toggle("in",inTune);
  c.textContent=inTune?"in tune":`${d.cents>0?"+":""}${Math.round(d.cents)} cents · ${d.cents>0?"sharp":"flat"}`;
  k.style.display="block";k.style.left=(50+ct)+"%";
}

// One update in flight at a time, so an older update can never land after
// a newer one. A change that did not reach the board (offline, server
// busy) is kept and retried, so the page never shows a value the LEDs are
// not using; edits made in the meantime win over the retried ones.
let inflight=null;
function send(){
  clearTimeout(timer);
  if(inflight)return inflight;          // the running send picks up new edits when it ends
  if(!Object.keys(pending).length){status("applied");return Promise.resolve();}
  inflight=(async()=>{
    for(;;){
      const body=pending;
      pending={};
      let retry=false;
      try{
        const r=await fetch("/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
        status(r.ok?"applied":"error "+r.status);
        retry=r.status>=500;
      }catch(e){status("offline, retrying");retry=true;}
      if(retry){pending=merge(body,pending);timer=setTimeout(send,1000);break;}
      if(!Object.keys(pending).length)break;   // edits arrived meanwhile: send them now
    }
    inflight=null;
  })();
  return inflight;
}
// Wait until no update is in flight (a retry may be queued in pending).
async function settle(){while(inflight)await inflight;}
function queue(p){if(!p)return;merge(pending,p);clearTimeout(timer);timer=setTimeout(send,120);}

// Keep highest note above lowest note.
function fixRange(t){
  const lo=$("pitchLow"),hi=$("pitchHigh");
  if(+hi.value>+lo.value)return;
  if(t===hi)lo.value=Math.max(MIDI_MIN,+hi.value-1);else hi.value=Math.min(MIDI_MAX,+lo.value+1);
}

// Keep full scale at least 6 dB above the noise gate.
function fixDb(t){
  const g=$("gateDb"),f=$("fullDb");
  if(+f.value>=+g.value+6)return;
  if(t===f)g.value=Math.max(-80,+f.value-6);else f.value=Math.min(0,+g.value+6);
}

function onEdit(e){
  if(e.target.dataset&&"local" in e.target.dataset)return;   // UI-only controls, not settings
  if(e.target===$("pitchLow")||e.target===$("pitchHigh"))fixRange(e.target);
  if(e.target===$("gateDb")||e.target===$("fullDb"))fixDb(e.target);
  if(e.target===$("mode"))desc();
  if(e.target===$("batteryFitted"))batRows();
  labels();
  document.querySelectorAll("#presets button.on").forEach(b=>b.classList.remove("on"));
  queue(patchFor(e.target));
}

async function loadEffects(){
  const d=await (await fetch("/api/effects")).json(), groups={};
  for(const e of d.effects){EFFECTS[e.mode]=e;(groups[e.group]=groups[e.group]||[]).push(e);}
  $("mode").innerHTML=Object.keys(groups).map(g=>`<optgroup label="${g}">`+
    groups[g].map(e=>`<option value="${e.mode}">${e.name}</option>`).join("")+"</optgroup>").join("");
}

// Effects first: fill() needs the effect options to select the saved one.
async function load(){
  try{await loadEffects();fill(await (await fetch("/api/config")).json());status("connected");}
  catch(e){status("connection failed");}
}
async function loadFirmwareVersion(){
  const badge=$("fwVersion");
  try{
    const r=await fetch("/api/version",{cache:"no-store"});
    if(!r.ok)throw new Error(r.status);
    const d=await r.json();
    configureLedCount(d.ledCount);
    badge.textContent="Firmware v"+d.version+" · online";
    badge.className="chip ok";
  }catch(e){
    badge.textContent="Firmware version unavailable";
    badge.className="chip bad";
  }
}

// Presets: one button each, with a saved per-preset note palette.
let presetsData=[];
function renderPresetPalette(index){
  const preset=presetsData[+index];
  if(!preset)return;
  $("presetPalette").innerHTML=NOTES.map((n,i)=>
    `<label class="note"><input type="color" data-preset-color="${i}" value="${preset.colors[i]}">${n}</label>`
  ).join("");
}
async function loadPresets(){
  try{
    const [namesResponse,palettesResponse]=await Promise.all([
      fetch("/api/presets",{cache:"no-store"}),
      fetch("/api/preset/palettes",{cache:"no-store"})
    ]);
    if(!namesResponse.ok||!palettesResponse.ok)throw new Error("preset list unavailable");
    const names=await namesResponse.json(),paletteData=await palettesResponse.json();
    presetsData=names.presets.map((name,i)=>({name,colors:paletteData.palettes[i]}));
    $("presets").innerHTML=presetsData.map((p,i)=>`<button data-i="${i}">${p.name}</button>`).join("");
    $("palettePreset").innerHTML=presetsData.map((p,i)=>`<option value="${i}">${p.name}</option>`).join("");
    renderPresetPalette(0);
  }catch(e){}
}
$("palettePreset").onchange=e=>renderPresetPalette(e.target.value);
$("savePresetPalette").onclick=async()=>{
  const index=+$("palettePreset").value;
  const colors=[...document.querySelectorAll("[data-preset-color]")].map(el=>el.value);
  const badge=$("paletteStatus");
  try{
    const r=await fetch("/api/preset/palette?i="+index,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({colors})});
    if(!r.ok)throw new Error(r.status);
    presetsData[index].colors=colors;
    const applied=await fetch("/api/preset?i="+index,{method:"POST"});
    if(!applied.ok)throw new Error(applied.status);
    fill(await applied.json());
    document.querySelectorAll("#presets button").forEach(b=>b.classList.toggle("on",+b.dataset.i===index));
    badge.textContent="Saved and applied";
    status("Preset palette saved: "+presetsData[index].name);
  }catch(e){badge.textContent="Could not save palette";}
};
$("presets").onclick=async e=>{
  const b=e.target.closest("button");
  if(!b)return;
  // The preset replaces these fields; send any other pending edit first.
  await settle();
  for(const k of ["mode","speed","trail","staticColor","noteColors"])delete pending[k];
  await send();
  try{
    const r=await fetch("/api/preset?i="+b.dataset.i,{method:"POST"});
    if(!r.ok)throw new Error(r.status);
    fill(await r.json());
    document.querySelectorAll("#presets button").forEach(x=>x.classList.toggle("on",x===b));
    $("palettePreset").value=b.dataset.i;
    renderPresetPalette(b.dataset.i);
    status("Preset: "+b.textContent);
  }catch(err){status("offline");}
};

// Mirror the real LEDs and the audio level while the page is visible.
let lastAttacks=null;
async function poll(){
  if(!document.hidden){
    try{
      const d=await (await fetch("/api/leds")).json(), s=$("strip");
      if(s.children.length!==d.leds.length){s.innerHTML="<b></b>".repeat(d.leds.length);$("ledCount").textContent=d.leds.length;}
      d.leds.forEach((h,i)=>{const b=s.children[i];b.style.background=h;b.style.boxShadow=`0 0 8px ${h}`;});
      $("meter").style.width=(d.level*100).toFixed(0)+"%";
      $("audioStat").textContent=`${d.db.toFixed(1)} dB · onset ${d.rise.toFixed(1)} dB · ${d.attacks} attacks`;
      $("pitchNow").textContent=d.note>=0
        ?`${name(d.note)} ${d.cents>=0?"+":""}${Math.round(d.cents)}¢ · ${Math.round(d.conf*100)}%`:"–";
      if(lastAttacks!==null&&d.attacks!==lastAttacks){
        const a=$("attackDot");a.classList.add("on");setTimeout(()=>a.classList.remove("on"),150);
      }
      lastAttacks=d.attacks;
      showTuner(d);
      showBattery(d.bat);
    }catch(e){}
  }
  setTimeout(poll,150);
}

// Guitar input scope: polls only while the card is open and the page is visible.
function drawAdc(d){
  const cv=$("scope"),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
  if(cv.width!==Math.round(W*dpr)){cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr);}
  const g=cv.getContext("2d");
  g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,W,H);
  const s=d.s||[];
  if(s.length>1){
    let lo=0,hi=4095;
    if($("zoom").checked){
      const mean=s.reduce((a,b)=>a+b,0)/s.length;
      let dev=64;
      for(const v of s)dev=Math.max(dev,Math.abs(v-mean)*1.25);
      lo=mean-dev;hi=mean+dev;
    }
    const y=v=>H-(v-lo)/(hi-lo)*H;
    g.fillStyle="rgba(122,37,48,.45)";   // outside the ADC's accurate range
    if(d.hi<hi)g.fillRect(0,0,W,Math.max(0,y(d.hi)));
    if(d.lo>lo){const t=Math.min(H,y(d.lo));g.fillRect(0,t,W,H-t);}
    g.strokeStyle="#5b8cff";g.lineWidth=1.5;g.beginPath();
    s.forEach((v,i)=>{const x=i/(s.length-1)*W;i?g.lineTo(x,y(v)):g.moveTo(x,y(v));});
    g.stroke();
  }
  $("adcStat").textContent=`DC ${d.dc.toFixed(3)} V · p-p ${d.p2p.toFixed(3)} V · RMS ${d.rms.toFixed(3)} V · `+
    `${(d.rate/1000).toFixed(2)} kHz · ${d.zc.toFixed(0)} crossings/s · ${(s.length/Math.max(d.rate,1)*1000).toFixed(0)} ms shown`;
  let st="Signal detected",cls="ok";
  if(!d.running){st="ADC not running";cls="bad";}
  else if(d.clip>0){st=`Clipping (${d.clip} samples)`;cls="bad";}
  else if(d.tone){st="Test tone";cls="info";}
  else if(!d.bias){st="Bias off (expect ~1.65 V)";cls="bad";}
  else if(d.p2p<0.02){st="No signal";cls="info";}
  else if(d.db<d.gate){st="Noise below gate";cls="warn";}
  const c=$("adcState");c.textContent=st;c.className="chip "+cls;
  const margin=d.gate-d.db;
  $("noiseMargin").textContent=`${d.db.toFixed(1)} dB / ${margin.toFixed(1)} dB margin`;
  if(!held("tone"))$("tone").checked=d.tone;
}
async function pollAdc(){
  if(!document.hidden&&$("inputCard").open){
    try{drawAdc(await (await fetch("/api/adc")).json());}catch(e){}
  }
  setTimeout(pollAdc,200);
}

async function pollCalibration(){
  if(!document.hidden&&$("inputCard").open){
    try{
      const c=await (await fetch("/api/calibrate")).json();
      if(c.active){$("calibrate").disabled=true;$("calStat").textContent=`Measuring quiet input: ${c.progress}%`;}
      else if(c.progress===100){
        $("calibrate").disabled=false;
        $("calStat").textContent=c.bias
          ?`Noise floor ${c.noise.toFixed(1)} dB. Suggested gate: ${c.gate} dB.`
          :"Bias is invalid; repair the analog input before calibrating.";
        $("calibrate").textContent=c.bias?`Use ${c.gate} dB`:"Measure";
      } else {$("calibrate").disabled=false;$("calibrate").textContent="Measure";}
    }catch(e){}
  }
  setTimeout(pollCalibration,300);
}

let LED_COUNT=10;
const ledTestState=Array.from({length:LED_COUNT},()=>({on:false,color:"#ff0000"}));
function configureLedCount(count){
  if(!Number.isInteger(count)||count<1||count>64)return;
  if(count!==LED_COUNT){
    LED_COUNT=count;
    ledTestState.length=0;
    for(let i=0;i<count;i++)ledTestState.push({on:false,color:"#ff0000"});
  }
  $("ledCount").textContent=count;
  if($("strip").children.length!==count)$("strip").innerHTML="<b></b>".repeat(count);
  renderLedTestControls();
}
function renderLedTestControls(){
  $("ledTestGrid").innerHTML=ledTestState.map((x,i)=>
    `<div class="ledcell"><span>LED ${i+1}</span><input type="color" data-led-color="${i}" value="${x.color}" aria-label="LED ${i+1} color"><label><input type="checkbox" data-led-on="${i}" ${x.on?"checked":""}>On</label></div>`
  ).join("");
}
let ledTestSending=false,ledTestQueued=false,ledTestDesiredActive=true;
let ledChaseTimer=null,ledChaseIndex=0;
function stopLedChase(){if(ledChaseTimer){clearInterval(ledChaseTimer);ledChaseTimer=null;}}
function syncLedCheckboxes(){
  ledTestState.forEach((x,i)=>{const el=document.querySelector(`[data-led-on="${i}"]`);if(el)el.checked=x.on;});
}
async function sendLedTest(active=true){
  const badge=$("ledTestStatus");
  ledTestDesiredActive=active;
  ledTestQueued=true;
  if(ledTestSending)return;
  ledTestSending=true;
  while(ledTestQueued){
    ledTestQueued=false;
    const body=JSON.stringify({active:ledTestDesiredActive,leds:ledTestState.map(x=>({...x}))});
    try{
      const r=await fetch("/api/led-debug",{method:"POST",headers:{"Content-Type":"application/json"},body});
      if(!r.ok)throw new Error(r.status);
      const d=await r.json();
      badge.textContent=d.active?"Individual LED test is active (full brightness).":"Test mode is off.";
      badge.className="hint chip "+(d.active?"info":"");
    }catch(e){badge.textContent="Could not reach the LED test endpoint.";badge.className="hint chip bad";}
  }
  ledTestSending=false;
}
$("ledTestGrid").addEventListener("change",e=>{
  const color=e.target.dataset.ledColor,on=e.target.dataset.ledOn;
  stopLedChase();
  if(color!==undefined){ledTestState[+color].color=e.target.value;ledTestState[+color].on=true;syncLedCheckboxes();}
  if(on!==undefined)ledTestState[+on].on=e.target.checked;
  sendLedTest(true);
});
$("ledAllOff").onclick=()=>{stopLedChase();ledTestState.forEach(x=>x.on=false);syncLedCheckboxes();sendLedTest(true);};
$("ledAllOn").onclick=()=>{stopLedChase();ledTestState.forEach(x=>x.on=true);syncLedCheckboxes();sendLedTest(true);};
$("ledChase").onclick=()=>{
  stopLedChase();ledChaseIndex=0;
  const step=()=>{
    ledTestState.forEach(x=>x.on=false);
    ledTestState[ledChaseIndex].on=true;
    syncLedCheckboxes();sendLedTest(true);
    ledChaseIndex=(ledChaseIndex+1)%ledTestState.length;
  };
  step();ledChaseTimer=setInterval(step,500);
};
$("ledTestExit").onclick=()=>{stopLedChase();sendLedTest(false);};
renderLedTestControls();

document.addEventListener("input",onEdit);
document.addEventListener("change",onEdit);
$("tone").onchange=e=>(hold("tone"),fetch("/api/tone?on="+(e.target.checked?1:0),{method:"POST"}).catch(()=>status("offline")));
$("save").onclick=send;
$("tunerOn").onchange=e=>(hold("tunerOn"),fetch("/api/tuner?on="+(e.target.checked?1:0),{method:"POST"}).catch(()=>status("offline")));
$("batShow").onclick=()=>fetch("/api/battery/show",{method:"POST"}).catch(()=>status("offline"));
$("pluck").onclick=()=>fetch("/api/pluck",{method:"POST"}).catch(()=>status("offline"));
$("calibrate").onclick=async()=>{
  const suggestion=/Use (-?\d+) dB/.exec($("calibrate").textContent);
  if(suggestion){
    $("gateDb").value=+suggestion[1];fixDb($("gateDb"));labels();queue(patchFor($("gateDb")));
    $("calStat").textContent=`Noise gate set to ${$("gateDb").value} dB.`;
    $("calibrate").textContent="Measure";return;
  }
  if(+$("source").value!==1){$("calStat").textContent="Select Guitar input (GPIO34) first.";return;}
  try{
    const r=await fetch("/api/calibrate",{method:"POST"});
    if(!r.ok)throw new Error(r.status);
    $("calibrate").disabled=true;$("calStat").textContent="Measuring quiet input: 0%";
  }catch(e){$("calStat").textContent="Calibration could not start.";}
};
$("reset").onclick=async()=>{
  if(!confirm("Reset all LED settings to defaults?"))return;
  try{fill(await (await fetch("/api/reset",{method:"POST"})).json());status("defaults restored");}
  catch(e){status("offline");}
};
load();
loadFirmwareVersion();
loadPresets();
poll();
pollAdc();
pollCalibration();
</script>
</body></html>
)rawhtml";

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleGetConfig() {
  server.send(200, "application/json", ConfigStore::toJson());
}

static void handleLedDebug() {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  JsonArrayConst pixels = doc["leds"].as<JsonArrayConst>();
  if (err || pixels.size() != ledCount || ledCount > MAX_DEBUG_LEDS || doc["active"].isNull()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"expected active and one color/on object per LED\"}");
    return;
  }
  for (uint16_t i = 0; i < ledCount; ++i) {
    const char* color = pixels[i]["color"] | "#000000";
    if (strlen(color) != 7 || color[0] != '#') {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid LED color\"}");
      return;
    }
    char* end = nullptr;
    const uint32_t rgb = strtoul(color + 1, &end, 16);
    if (!end || *end != '\0') {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid LED color\"}");
      return;
    }
    debugColors[i] = CRGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    debugOn[i] = pixels[i]["on"] | false;
  }
  debugActive = doc["active"].as<bool>();
  server.send(200, "application/json", debugActive ? "{\"ok\":true,\"active\":true}" : "{\"ok\":true,\"active\":false}");
}

static void handleGetFirmwareVersion() {
  String out = "{\"version\":\"" FIRMWARE_VERSION_STRING "\",\"ledCount\":";
  out += String(ledCount);
  out += ",\"major\":";
  out += String(FIRMWARE_VERSION_MAJOR);
  out += ",\"minor\":";
  out += String(FIRMWARE_VERSION_MINOR);
  out += ",\"patch\":";
  out += String(FIRMWARE_VERSION_PATCH);
  out += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void handlePostConfig() {
  String err;
  if (ConfigStore::fromJson(server.arg("plain"), err)) {
    ConfigStore::requestSave();
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
  }
}

static void handleReset() {
  ConfigStore::setDefaults();
  ConfigStore::save();
  server.send(200, "application/json", ConfigStore::toJson());
}

static void handlePresets() {
  String out = "{\"presets\":[";
  for (size_t i = 0; i < Presets::count(); i++) {
    if (i) out += ',';
    out += '"'; out += Presets::name(i); out += '"';
  }
  out += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void handlePresetPalettes() {
  JsonDocument doc;
  JsonArray presets = doc["palettes"].to<JsonArray>();
  for (size_t i = 0; i < Presets::count(); ++i) {
    RGBColor colors[12];
    Presets::getPalette(i, colors);
    JsonArray palette = presets.add<JsonArray>();
    for (size_t n = 0; n < 12; ++n) {
      char hex[8];
      snprintf(hex, sizeof(hex), "#%02x%02x%02x", colors[n].r, colors[n].g, colors[n].b);
      palette.add(hex);
    }
  }
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void handleEffects() {
  JsonDocument doc;
  JsonArray list = doc["effects"].to<JsonArray>();
  for (size_t i = 0; i < EffectCatalog::count(); i++) {
    const EffectCatalog::Info& e = EffectCatalog::at(i);
    JsonObject o = list.add<JsonObject>();
    o["mode"]  = (int)e.mode;
    o["group"] = e.group;
    o["name"]  = e.name;
    o["desc"]  = e.desc;
    o["uses"]  = e.uses;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePreset() {
  size_t i = 0;
  if (Presets::parseIndex(server.arg("i").c_str(), i) && Presets::apply(i)) {
    server.send(200, "application/json", ConfigStore::toJson());
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no such preset\"}");
  }
}

static void handleSavePresetPalette() {
  size_t index = 0;
  if (!Presets::parseIndex(server.arg("i").c_str(), index)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no such preset\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  JsonArrayConst palette = doc["colors"].as<JsonArrayConst>();
  if (err || palette.size() != 12) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"expected 12 hex colors\"}");
    return;
  }
  RGBColor colors[12];
  for (size_t i = 0; i < 12; ++i) {
    const char* text = palette[i] | "";
    if (strlen(text) != 7 || text[0] != '#') {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid color\"}");
      return;
    }
    for (size_t c = 1; c < 7; ++c) {
      if (!isxdigit((unsigned char)text[c])) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid color\"}");
        return;
      }
    }
    const uint32_t rgb = strtoul(text + 1, nullptr, 16);
    colors[i] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
  }
  if (!Presets::setPalette(index, colors)) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"palette storage failed\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleLeds() {
  String out;
  out.reserve(ledCount * 10 + 260);
  out += "{\"leds\":[";
  char b[160];
  for (uint16_t i = 0; i < ledCount; i++) {
    snprintf(b, sizeof(b), "%s\"#%02x%02x%02x\"", i ? "," : "", ledBuf[i].r, ledBuf[i].g, ledBuf[i].b);
    out += b;
  }
  const AudioDsp::Snapshot d   = AudioDsp::snapshot();
  const Battery::Status    bat = Battery::status();
  snprintf(b, sizeof(b),
           "],\"level\":%.3f,\"db\":%.1f,\"rise\":%.1f,\"attacks\":%u,\"note\":%d,\"cents\":%.1f,\"conf\":%.2f",
           d.level, d.db, d.rise, (unsigned)d.attacks, d.pitchValid ? d.note : -1, d.cents, d.confidence);
  out += b;
  snprintf(b, sizeof(b),
           ",\"tuner\":%s,\"bat\":{\"fitted\":%s,\"state\":\"%s\",\"v\":%.2f,\"pct\":%d,\"bootOnly\":%s}}",
           Stage::tuner() ? "true" : "false", bat.fitted ? "true" : "false",
           BatteryMonitor::name(bat.state), bat.volts, bat.percent, bat.bootOnly ? "true" : "false");
  out += b;
  server.send(200, "application/json", out);
}

static void handlePluck() {
  GuitarSynth::pluck();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAdc() {
  static uint16_t s[AudioInput::SCOPE_LEN];
  const AudioInput::Stats a = AudioInput::stats();
  const AudioDsp::Snapshot d = AudioDsp::snapshot();
  const Settings settings = ConfigStore::snapshot();
  const size_t n = AudioInput::scope(s, AudioInput::SCOPE_LEN);

  String out;
  out.reserve(300 + n * 5);
  char b[300];
  snprintf(b, sizeof(b),
           "{\"running\":%s,\"rate\":%.1f,\"dc\":%.4f,\"min\":%.4f,\"max\":%.4f,\"p2p\":%.4f,"
           "\"rms\":%.4f,\"clip\":%u,\"zc\":%.1f,\"lo\":%u,\"hi\":%u,\"bias\":%s,\"db\":%.1f,\"gate\":%d,\"tone\":%s,\"s\":[",
           a.running ? "true" : "false", a.sampleRate, a.dcV, a.minV, a.maxV, a.p2pV,
           a.rmsV, (unsigned)a.clipped, a.zeroCrossHz, a.rawLo, a.rawHi, a.biasValid ? "true" : "false",
           d.db, settings.gateDb,
           TestTone::active() ? "true" : "false");
  out += b;
  for (size_t i = 0; i < n; i++) {
    if (i) out += ',';
    out += s[i];
  }
  out += "]}";
  server.send(200, "application/json", out);
}

static void handleGetCalibration() {
  const AudioInput::Calibration c = AudioInput::calibration();
  char out[128];
  snprintf(out, sizeof(out), "{\"active\":%s,\"bias\":%s,\"progress\":%u,\"noise\":%.1f,\"gate\":%d}",
           c.active ? "true" : "false", c.biasValid ? "true" : "false",
           (unsigned)c.progress, c.noiseDb, c.suggestedGateDb);
  server.send(200, "application/json", out);
}

static void handleStartCalibration() {
  if (ConfigStore::snapshot().audioSource != SOURCE_GUITAR) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"select guitar input first\"}");
    return;
  }
  if (!AudioInput::startNoiseCalibration()) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"calibration already running\"}");
    return;
  }
  server.send(202, "application/json", "{\"ok\":true}");
}

static void handleTuner() {
  Stage::setTuner(server.arg("on") == "1");
  server.send(200, "application/json", Stage::tuner() ? "{\"tuner\":true}" : "{\"tuner\":false}");
}

static void handleBatteryShow() {
  Stage::showBatteryGauge(millis());
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleTone() {
  TestTone::set(server.arg("on") == "1");
  server.send(200, "application/json", TestTone::active() ? "{\"tone\":true}" : "{\"tone\":false}");
}

bool webPortalDebugActive() { return debugActive; }

void webPortalRenderDebug() {
  if (!debugActive || !ledBuf) return;
  for (uint16_t i = 0; i < ledCount; ++i) ledBuf[i] = debugOn[i] ? debugColors[i] : CRGB::Black;
}

void webPortalBegin(CRGB* leds, uint16_t numLeds) {
  if (!EffectCatalog::selfCheck()) Serial.println(F("error: EffectCatalog lists an effect twice"));
  ledBuf   = leds;
  ledCount = numLeds;

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/api/config", HTTP_GET,  handleGetConfig);
  server.on("/api/led-debug", HTTP_POST, handleLedDebug);
  server.on("/api/version", HTTP_GET, handleGetFirmwareVersion);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/reset",  HTTP_POST, handleReset);
  server.on("/api/leds",   HTTP_GET,  handleLeds);
  server.on("/api/pluck",  HTTP_POST, handlePluck);
  server.on("/api/adc",    HTTP_GET,  handleAdc);
  server.on("/api/calibrate", HTTP_GET, handleGetCalibration);
  server.on("/api/calibrate", HTTP_POST, handleStartCalibration);
  server.on("/api/tone",   HTTP_POST, handleTone);
  server.on("/api/presets", HTTP_GET, handlePresets);
  server.on("/api/preset/palettes", HTTP_GET, handlePresetPalettes);
  server.on("/api/preset/palette", HTTP_POST, handleSavePresetPalette);
  server.on("/api/effects", HTTP_GET, handleEffects);
  server.on("/api/tuner",   HTTP_POST, handleTuner);
  server.on("/api/battery/show", HTTP_POST, handleBatteryShow);
  server.on("/api/preset", HTTP_POST, handlePreset);
  server.onNotFound(handleRoot);   // any other path shows the UI
  server.begin();
}

void webPortalLoop() { server.handleClient(); }
