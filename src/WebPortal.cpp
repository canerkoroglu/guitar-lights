#include "WebPortal.h"
#include <WebServer.h>
#include "Config.h"
#include "AudioDsp.h"
#include "GuitarSynth.h"
#include "AudioInput.h"
#include "TestTone.h"
#include "Presets.h"

static WebServer   server(80);
static const CRGB* ledBuf   = nullptr;
static uint16_t    ledCount = 0;

static const char INDEX_HTML[] PROGMEM = R"rawhtml(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Guitar RGB</title>
<style>
:root{--bg:#0e0f13;--card:#1a1d24;--line:#2a2f3a;--fg:#e7e9ee;--mut:#8a90a0;--acc:#5b8cff}
*{box-sizing:border-box}
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
@media (max-width:420px){.notes{grid-template-columns:repeat(4,1fr)}}
</style></head><body>
<h1>Guitar RGB</h1>
<p class="sub">16 &times; WS2812B &middot; <span id="stat">connecting&hellip;</span></p>

<div class="card">
  <h2>Live</h2>
  <div class="strip" id="strip"></div>
</div>

<div class="card">
  <h2>Presets</h2>
  <div class="presets" id="presets"></div>
</div>

<details class="card" id="inputCard">
  <summary><h2>Guitar input (GPIO34)</h2></summary>
  <canvas id="scope"></canvas>
  <p class="hint" id="adcStat">Waiting for samples&hellip;</p>
  <div class="row"><span>Status</span><b class="chip" id="adcState">&ndash;</b></div>
  <div class="row"><span>Zoom to signal</span><label class="sw"><input type="checkbox" id="zoom" data-local checked><i></i></label></div>
  <div class="row"><span>Test tone on GPIO26</span><label class="sw"><input type="checkbox" id="tone" data-local><i></i></label></div>
  <p class="hint">Loopback test: jumper GPIO26 to GPIO34 with nothing else on GPIO34. Remove the jumper before wiring the buffer circuit.</p>
</details>

<div class="card">
  <h2>Output</h2>
  <div class="row"><span>Power</span><label class="sw"><input type="checkbox" id="power"><i></i></label></div>
  <div class="row"><span>Brightness</span><input type="range" id="brightness" min="0" max="255"></div>
  <div class="row"><span>Effect</span>
    <select id="mode">
      <optgroup label="Ambient">
        <option value="0">Static color</option>
        <option value="1">Rainbow</option>
        <option value="2">Breathing</option>
        <option value="9">Twinkle</option>
        <option value="10">Scanner</option>
        <option value="11">Ocean</option>
        <option value="12">Lava</option>
      </optgroup>
      <optgroup label="Whole strip (audio)">
        <option value="3">VU meter</option>
        <option value="4">Pitch color</option>
        <option value="15">Center VU</option>
        <option value="13">Peak flash</option>
        <option value="14">Sustain</option>
        <option value="16">Fire</option>
      </optgroup>
      <optgroup label="Moving along strip (audio)">
        <option value="5">Pitch position</option>
        <option value="6">Pitch direction</option>
        <option value="7">Note flow</option>
        <option value="8">Ripple</option>
        <option value="17">Sparkle</option>
        <option value="18">Comet</option>
        <option value="19">Step chase</option>
      </optgroup>
    </select></div>
  <p class="hint" id="desc"></p>
  <div class="row"><span>Speed</span><input type="range" id="speed" min="1" max="100"></div>
  <div class="row"><span>Trail</span><input type="range" id="trail" min="1" max="100"></div>
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
  <p class="hint" id="audioStat">&ndash;</p>
  <div class="row"><span>Noise gate<small id="gateDbV"></small></span><input type="range" id="gateDb" min="-80" max="-10"></div>
  <div class="row"><span>Full scale<small id="fullDbV"></small></span><input type="range" id="fullDb" min="-40" max="0"></div>
  <div class="row"><span>Release<small id="releaseMsV"></small></span><input type="range" id="releaseMs" min="20" max="1000" step="10"></div>
  <div class="row"><span>Attack threshold<small id="attackDbV"></small></span><input type="range" id="attackDb" min="2" max="30"></div>
  <p class="hint">Noise gate: anything quieter counts as silence. Full scale: this loud lights every LED. Release: how fast the level falls. Attack threshold: how sharp a jump counts as a pick; lower is more sensitive.</p>
</div>

<div class="card">
  <h2>Simulated guitar</h2>
  <p class="hint">Used when Source is Simulated guitar. Auto-play picks notes with varied strength, slides and rests. Turn it off to pick a note yourself.</p>
  <div class="row"><span>Auto-play</span><label class="sw"><input type="checkbox" id="testAuto"><i></i></label></div>
  <div class="row"><span>Pick strength</span><input type="range" id="testLevel" min="0" max="100"></div>
  <div class="row"><span>Note</span><select id="testNote"></select></div>
  <div class="row"><span>Pick</span><button id="pluck">Pluck</button></div>
</div>

<div class="btns">
  <button class="ghost" id="reset">Reset defaults</button>
  <button id="save">Apply now</button>
</div>

<script>
const NOTES=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
const DESC=[
  "One color on every LED.",
  "Rainbow scrolling along the strip.",
  "Static color slowly fading in and out.",
  "Number of lit LEDs follows how hard you play.",
  "Whole strip switches to the color of the current note.",
  "A dot sits where the note falls between the lowest and highest note, leaving a trail as you move.",
  "Bands travel up the strip as pitch rises and down as it falls. Holds still on a steady note.",
  "Each note's color enters at one end and scrolls along, showing what you just played.",
  "Each pick sends pulses outward from the note's position.",
  "Random LEDs light in the static color and fade, like stars. Speed: how many. Trail: how long they glow.",
  "One dot in the static color sweeps end to end with a fading tail.",
  "Blues and greens drifting slowly along the strip.",
  "Reds and oranges drifting slowly along the strip.",
  "VU meter, plus a white flash on every pick.",
  "Whole strip in the note color: bright on a pick, follows the ringing note, then fades. Trail: fade time.",
  "Level bar grows from the middle out to both ends.",
  "Flames rise from the start of the strip; playing and picks feed the fire. Trail: flame height.",
  "Each pick lights random LEDs in the note color; harder picks, more sparks.",
  "Each pick launches a comet in the note color; harder picks fly faster.",
  "A block of 4 LEDs steps along the strip on every pick."
];
// Which of Speed (s), Trail (t) and Static color (c) each effect uses, by effect number.
const USES=["c","s","sc","","","t","st","st","st","stc","stc","s","s","","tc","","t","tc","stc","tc"];
const $=id=>document.getElementById(id);
const name=m=>NOTES[m%12]+(Math.floor(m/12)-1);
const range=(a,b)=>{let s="";for(let m=a;m<=b;m++)s+=`<option value="${m}">${name(m)}</option>`;return s;};
let timer=null;

$("strip").innerHTML="<b></b>".repeat(16);
$("notes").innerHTML=NOTES.map((n,i)=>`<label class="note"><input type="color" data-note="${i}">${n}</label>`).join("");
$("pitchLow").innerHTML=range(28,95);
$("pitchHigh").innerHTML=range(29,96);
$("testNote").innerHTML='<option value="-1">none</option>'+range(28,96);

// Effect description, and dim the rows the effect ignores.
function desc(){
  const m=+$("mode").value, u=USES[m]||"";
  $("desc").textContent=DESC[m]||"";
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
  $("testAuto").checked=c.test.auto;
  $("testLevel").value=Math.round(c.test.level*100);
  $("testNote").value=c.test.note;
  desc();
  labels();
}

function labels(){
  $("gateDbV").textContent=$("gateDb").value+" dB";
  $("fullDbV").textContent=$("fullDb").value+" dB";
  $("releaseMsV").textContent=$("releaseMs").value+" ms";
  $("attackDbV").textContent=$("attackDb").value+" dB";
}

function collect(){
  return {
    power:$("power").checked, brightness:+$("brightness").value, mode:+$("mode").value,
    speed:+$("speed").value, trail:+$("trail").value, staticColor:$("staticColor").value,
    noteColors:[...document.querySelectorAll("[data-note]")].map(el=>el.value),
    pitchLow:+$("pitchLow").value, pitchHigh:+$("pitchHigh").value, reverse:$("reverse").checked,
    source:+$("source").value, gateDb:+$("gateDb").value, fullDb:+$("fullDb").value,
    releaseMs:+$("releaseMs").value, attackDb:+$("attackDb").value,
    test:{auto:$("testAuto").checked, level:+$("testLevel").value/100, note:+$("testNote").value}
  };
}

function status(t){$("stat").textContent=t;}

async function send(){
  try{
    const r=await fetch("/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(collect())});
    status(r.ok?"applied":"error "+r.status);
  }catch(e){status("offline");}
}
function queue(){clearTimeout(timer);timer=setTimeout(send,120);}

// Keep highest note above lowest note.
function fixRange(t){
  const lo=$("pitchLow"),hi=$("pitchHigh");
  if(+hi.value>+lo.value)return;
  if(t===hi)lo.value=Math.max(28,+hi.value-1);else hi.value=Math.min(96,+lo.value+1);
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
  labels();
  document.querySelectorAll("#presets button.on").forEach(b=>b.classList.remove("on"));
  queue();
}

async function load(){
  try{fill(await (await fetch("/api/config")).json());status("connected");}
  catch(e){status("connection failed");}
}

// Presets: one button each. Applying one refreshes every control.
async function loadPresets(){
  try{
    const d=await (await fetch("/api/presets")).json();
    $("presets").innerHTML=d.presets.map((n,i)=>`<button data-i="${i}">${n}</button>`).join("");
  }catch(e){}
}
$("presets").onclick=async e=>{
  const b=e.target.closest("button");
  if(!b)return;
  clearTimeout(timer);
  try{
    const r=await fetch("/api/preset?i="+b.dataset.i,{method:"POST"});
    if(!r.ok)throw new Error(r.status);
    fill(await r.json());
    document.querySelectorAll("#presets button").forEach(x=>x.classList.toggle("on",x===b));
    status("Preset: "+b.textContent);
  }catch(err){status("offline");}
};

// Mirror the real LEDs and the audio level while the page is visible.
let lastAttacks=null;
async function poll(){
  if(!document.hidden){
    try{
      const d=await (await fetch("/api/leds")).json(), s=$("strip");
      if(s.children.length!==d.leds.length)s.innerHTML="<b></b>".repeat(d.leds.length);
      d.leds.forEach((h,i)=>{const b=s.children[i];b.style.background=h;b.style.boxShadow=`0 0 8px ${h}`;});
      $("meter").style.width=(d.level*100).toFixed(0)+"%";
      $("audioStat").textContent=`${d.db.toFixed(1)} dB · onset ${d.rise.toFixed(1)} dB · ${d.attacks} attacks`;
      if(lastAttacks!==null&&d.attacks!==lastAttacks){
        const a=$("attackDot");a.classList.add("on");setTimeout(()=>a.classList.remove("on"),150);
      }
      lastAttacks=d.attacks;
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
  let st="OK",cls="ok";
  if(!d.running){st="ADC not running";cls="bad";}
  else if(d.clip>0){st=`Clipping (${d.clip} samples)`;cls="bad";}
  else if(d.tone){st="Test tone";cls="info";}
  else if(d.dc<1.4||d.dc>1.9){st="Bias off (expect ~1.65 V)";cls="bad";}
  else if(d.p2p<0.02){st="No signal";cls="info";}
  const c=$("adcState");c.textContent=st;c.className="chip "+cls;
  $("tone").checked=d.tone;
}
async function pollAdc(){
  if(!document.hidden&&$("inputCard").open){
    try{drawAdc(await (await fetch("/api/adc")).json());}catch(e){}
  }
  setTimeout(pollAdc,200);
}

document.addEventListener("input",onEdit);
document.addEventListener("change",onEdit);
$("tone").onchange=e=>fetch("/api/tone?on="+(e.target.checked?1:0),{method:"POST"}).catch(()=>status("offline"));
$("save").onclick=send;
$("pluck").onclick=()=>fetch("/api/pluck",{method:"POST"}).catch(()=>status("offline"));
$("reset").onclick=async()=>{
  if(!confirm("Reset all LED settings to defaults?"))return;
  try{fill(await (await fetch("/api/reset",{method:"POST"})).json());status("defaults restored");}
  catch(e){status("offline");}
};
load();
loadPresets();
poll();
pollAdc();
</script>
</body></html>
)rawhtml";

static void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

static void handleGetConfig() {
  server.send(200, "application/json", ConfigStore::toJson());
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
    out += '"';
    out += Presets::name(i);
    out += '"';
  }
  out += "]}";
  server.send(200, "application/json", out);
}

static void handlePreset() {
  const String i = server.arg("i");
  if (i.length() && Presets::apply((size_t)i.toInt())) {
    server.send(200, "application/json", ConfigStore::toJson());
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no such preset\"}");
  }
}

static void handleLeds() {
  String out;
  out.reserve(ledCount * 10 + 100);
  out += "{\"leds\":[";
  char b[100];
  for (uint16_t i = 0; i < ledCount; i++) {
    snprintf(b, sizeof(b), "%s\"#%02x%02x%02x\"", i ? "," : "", ledBuf[i].r, ledBuf[i].g, ledBuf[i].b);
    out += b;
  }
  const AudioDsp::Snapshot d = AudioDsp::snapshot();
  snprintf(b, sizeof(b), "],\"level\":%.3f,\"db\":%.1f,\"rise\":%.1f,\"attacks\":%u}",
           d.level, d.db, d.rise, (unsigned)d.attacks);
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
  const size_t n = AudioInput::scope(s, AudioInput::SCOPE_LEN);

  String out;
  out.reserve(300 + n * 5);
  char b[300];
  snprintf(b, sizeof(b),
           "{\"running\":%s,\"rate\":%.1f,\"dc\":%.4f,\"min\":%.4f,\"max\":%.4f,\"p2p\":%.4f,"
           "\"rms\":%.4f,\"clip\":%u,\"zc\":%.1f,\"lo\":%u,\"hi\":%u,\"tone\":%s,\"s\":[",
           a.running ? "true" : "false", a.sampleRate, a.dcV, a.minV, a.maxV, a.p2pV,
           a.rmsV, (unsigned)a.clipped, a.zeroCrossHz, a.rawLo, a.rawHi,
           TestTone::active() ? "true" : "false");
  out += b;
  for (size_t i = 0; i < n; i++) {
    if (i) out += ',';
    out += s[i];
  }
  out += "]}";
  server.send(200, "application/json", out);
}

static void handleTone() {
  TestTone::set(server.arg("on") == "1");
  server.send(200, "application/json", TestTone::active() ? "{\"tone\":true}" : "{\"tone\":false}");
}

void webPortalBegin(const CRGB* leds, uint16_t numLeds) {
  ledBuf   = leds;
  ledCount = numLeds;

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/api/config", HTTP_GET,  handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/reset",  HTTP_POST, handleReset);
  server.on("/api/leds",   HTTP_GET,  handleLeds);
  server.on("/api/pluck",  HTTP_POST, handlePluck);
  server.on("/api/adc",    HTTP_GET,  handleAdc);
  server.on("/api/tone",   HTTP_POST, handleTone);
  server.on("/api/presets", HTTP_GET, handlePresets);
  server.on("/api/preset", HTTP_POST, handlePreset);
  server.onNotFound(handleRoot);   // any other path shows the UI
  server.begin();
}

void webPortalLoop() { server.handleClient(); }
