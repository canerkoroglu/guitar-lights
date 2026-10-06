#include "WebPortal.h"
#include <WebServer.h>
#include "Config.h"
#include "AudioSim.h"

static WebServer   server(80);
static const CRGB* ledBuf   = nullptr;
static uint16_t    ledCount = 0;

// Changes apply to the LEDs immediately, but are written to flash (NVS)
// only after the UI has been idle for SAVE_DELAY_MS. Dragging a slider
// would otherwise hammer the flash with dozens of writes per second.
static constexpr uint32_t SAVE_DELAY_MS = 2000;
static bool     dirty      = false;
static uint32_t lastChange = 0;

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
@media (max-width:420px){.notes{grid-template-columns:repeat(4,1fr)}}
</style></head><body>
<h1>Guitar RGB</h1>
<p class="sub">16 &times; WS2812B &middot; <span id="stat">connecting&hellip;</span></p>

<div class="card">
  <h2>Live</h2>
  <div class="strip" id="strip"></div>
</div>

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
      </optgroup>
      <optgroup label="Whole strip (audio)">
        <option value="3">VU meter</option>
        <option value="4">Pitch color</option>
      </optgroup>
      <optgroup label="Moving along strip (audio)">
        <option value="5">Pitch position</option>
        <option value="6">Pitch direction</option>
        <option value="7">Note flow</option>
        <option value="8">Ripple</option>
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
  <h2>Test / simulate</h2>
  <p class="hint">No guitar input yet. Auto plays random notes with slides. Turn it off to hold a note yourself.</p>
  <div class="row"><span>Auto-play</span><label class="sw"><input type="checkbox" id="testAuto"><i></i></label></div>
  <div class="row"><span>Level</span><input type="range" id="testLevel" min="0" max="100"></div>
  <div class="row"><span>Note</span><select id="testNote"></select></div>
  <div class="row"><span>Pick attack</span><button id="pluck">Pluck</button></div>
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
  "Each pick sends pulses outward from the note's position."
];
const $=id=>document.getElementById(id);
const name=m=>NOTES[m%12]+(Math.floor(m/12)-1);
const range=(a,b)=>{let s="";for(let m=a;m<=b;m++)s+=`<option value="${m}">${name(m)}</option>`;return s;};
let timer=null;

$("strip").innerHTML="<b></b>".repeat(16);
$("notes").innerHTML=NOTES.map((n,i)=>`<label class="note"><input type="color" data-note="${i}">${n}</label>`).join("");
$("pitchLow").innerHTML=range(28,95);
$("pitchHigh").innerHTML=range(29,96);
$("testNote").innerHTML='<option value="-1">none</option>'+range(28,96);

function desc(){$("desc").textContent=DESC[+$("mode").value]||"";}

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
  $("testAuto").checked=c.test.auto;
  $("testLevel").value=Math.round(c.test.level*100);
  $("testNote").value=c.test.note;
  desc();
}

function collect(){
  return {
    power:$("power").checked, brightness:+$("brightness").value, mode:+$("mode").value,
    speed:+$("speed").value, trail:+$("trail").value, staticColor:$("staticColor").value,
    noteColors:[...document.querySelectorAll("[data-note]")].map(el=>el.value),
    pitchLow:+$("pitchLow").value, pitchHigh:+$("pitchHigh").value, reverse:$("reverse").checked,
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

function onEdit(e){
  if(e.target===$("pitchLow")||e.target===$("pitchHigh"))fixRange(e.target);
  if(e.target===$("mode"))desc();
  queue();
}

async function load(){
  try{fill(await (await fetch("/api/config")).json());status("connected");}
  catch(e){status("connection failed");}
}

// Mirror the real LEDs while the page is visible.
async function poll(){
  if(!document.hidden){
    try{
      const c=await (await fetch("/api/leds")).json(), s=$("strip");
      if(s.children.length!==c.length)s.innerHTML="<b></b>".repeat(c.length);
      c.forEach((h,i)=>{const b=s.children[i];b.style.background=h;b.style.boxShadow=`0 0 8px ${h}`;});
    }catch(e){}
  }
  setTimeout(poll,150);
}

document.addEventListener("input",onEdit);
document.addEventListener("change",onEdit);
$("save").onclick=send;
$("pluck").onclick=()=>fetch("/api/pluck",{method:"POST"}).catch(()=>status("offline"));
$("reset").onclick=async()=>{
  if(!confirm("Reset all LED settings to defaults?"))return;
  try{fill(await (await fetch("/api/reset",{method:"POST"})).json());status("defaults restored");}
  catch(e){status("offline");}
};
load();
poll();
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
    dirty = true;
    lastChange = millis();
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
  }
}

static void handleReset() {
  ConfigStore::setDefaults();
  ConfigStore::save();
  dirty = false;
  server.send(200, "application/json", ConfigStore::toJson());
}

static void handleLeds() {
  String out;
  out.reserve(ledCount * 10 + 2);
  out += '[';
  char b[12];
  for (uint16_t i = 0; i < ledCount; i++) {
    snprintf(b, sizeof(b), "%s\"#%02x%02x%02x\"", i ? "," : "", ledBuf[i].r, ledBuf[i].g, ledBuf[i].b);
    out += b;
  }
  out += ']';
  server.send(200, "application/json", out);
}

static void handlePluck() {
  audioSimPluck();
  server.send(200, "application/json", "{\"ok\":true}");
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
  server.onNotFound(handleRoot);   // any other path shows the UI
  server.begin();
}

void webPortalLoop() {
  server.handleClient();
  if (dirty && millis() - lastChange >= SAVE_DELAY_MS) {
    ConfigStore::save();
    dirty = false;
  }
}
