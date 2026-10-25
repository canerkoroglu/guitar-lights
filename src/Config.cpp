#include "Config.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Settings    g;
static Preferences prefs;

// Only the loop task writes g, but the audio task reads it. Writers build a
// complete new Settings and swap it in under this lock; other tasks read a
// consistent copy with snapshot(), never a half-applied update.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static void commit(const Settings& next) {
  portENTER_CRITICAL(&mux);
  g = next;
  portEXIT_CRITICAL(&mux);
}

// Changes apply immediately but are written to flash (NVS) only after
// SAVE_DELAY_MS without further changes. Dragging a slider would otherwise
// hammer the flash with dozens of writes per second.
static constexpr uint32_t SAVE_DELAY_MS = 2000;
static bool     dirty      = false;
static uint32_t lastChange = 0;

static String toHex(const RGBColor& c) {
  char b[8];
  snprintf(b, sizeof(b), "#%02x%02x%02x", c.r, c.g, c.b);
  return String(b);
}

// "#rrggbb" or "rrggbb". Returns false (out untouched) for anything else.
static bool parseHex(const char* s, RGBColor& out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  for (int i = 0; i < 6; i++) if (!isxdigit((unsigned char)s[i])) return false;
  const long v = strtol(s, nullptr, 16);
  out = {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  return true;
}

void ConfigStore::setDefaults() {
  Settings d{};
  d.power       = true;
  d.brightness  = 64;
  d.mode        = MODE_RAINBOW;
  d.speed       = 50;
  d.trail       = 50;
  d.staticColor = {255, 0, 0};
  d.pitchLow    = 40;   // E2, lowest open string
  d.pitchHigh   = 76;   // E5 (readme.md Phase 4 target range)
  d.reverse     = false;

  // 12-step hue wheel, C = red ... B = rose (readme.md "Pitch Color" example).
  static const uint32_t pal[12] = {
    0xff0000, 0xff7f00, 0xffff00, 0x7fff00, 0x00ff00, 0x00ff7f,
    0x00ffff, 0x007fff, 0x0000ff, 0x7f00ff, 0xff00ff, 0xff007f
  };
  for (int i = 0; i < 12; i++) {
    d.noteColors[i] = {(uint8_t)(pal[i] >> 16), (uint8_t)(pal[i] >> 8), (uint8_t)pal[i]};
  }

  d.audioSource = SOURCE_SIMULATED;   // no input circuit yet
  d.gateDb      = -45;
  d.fullDb      = -6;
  d.releaseMs   = 120;
  d.attackDb    = 6;
  d.pitchConf   = 85;

  d.testAuto  = true;
  d.testLevel = 0.7f;
  d.testNote  = -1;

  d.batteryFitted = false;   // the divider is optional hardware
  d.batteryTrimMv = 0;

  commit(d);
}

const Settings& ConfigStore::get() { return g; }

Settings ConfigStore::snapshot() {
  portENTER_CRITICAL(&mux);
  const Settings copy = g;
  portEXIT_CRITICAL(&mux);
  return copy;
}

void ConfigStore::begin() {
  setDefaults();
  prefs.begin("guitarrgb", false);
  String s = prefs.getString("cfg", "");
  if (s.length()) {
    String err;
    fromJson(s, err);   // merge saved values over defaults
  }
}

void ConfigStore::save() {
  prefs.putString("cfg", toJson());
  dirty = false;
}

void ConfigStore::requestSave() {
  dirty      = true;
  lastChange = millis();
}

void ConfigStore::loop() {
  if (dirty && millis() - lastChange >= SAVE_DELAY_MS) save();
}

String ConfigStore::toJson() {
  JsonDocument doc;
  doc["power"]       = g.power;
  doc["brightness"]  = g.brightness;
  doc["mode"]        = g.mode;
  doc["speed"]       = g.speed;
  doc["trail"]       = g.trail;
  doc["staticColor"] = toHex(g.staticColor);
  doc["pitchLow"]    = g.pitchLow;
  doc["pitchHigh"]   = g.pitchHigh;
  doc["reverse"]     = g.reverse;

  JsonArray nc = doc["noteColors"].to<JsonArray>();
  for (int i = 0; i < 12; i++) nc.add(toHex(g.noteColors[i]));

  doc["source"]    = g.audioSource;
  doc["gateDb"]    = g.gateDb;
  doc["fullDb"]    = g.fullDb;
  doc["releaseMs"] = g.releaseMs;
  doc["attackDb"]  = g.attackDb;
  doc["pitchConf"] = g.pitchConf;

  JsonObject t = doc["test"].to<JsonObject>();
  t["auto"]  = g.testAuto;
  t["level"] = g.testLevel;
  t["note"]  = g.testNote;

  JsonObject b = doc["battery"].to<JsonObject>();
  b["fitted"] = g.batteryFitted;
  b["trimMv"] = g.batteryTrimMv;

  String out;
  serializeJson(doc, out);
  return out;
}

bool ConfigStore::fromJson(const String& body, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) { err = e.c_str(); return false; }

  // Partial updates are allowed: only keys present in the body change.
  // Work on a copy so the audio task never sees a half-applied update.
  Settings n = g;
  if (!doc["power"].isNull())       n.power      = doc["power"].as<bool>();
  if (!doc["brightness"].isNull())  n.brightness = constrain(doc["brightness"].as<int>(), 0, 255);
  if (!doc["mode"].isNull())        n.mode       = constrain(doc["mode"].as<int>(), 0, (int)MODE_COUNT - 1);
  if (!doc["speed"].isNull())       n.speed      = constrain(doc["speed"].as<int>(), 1, 100);
  if (!doc["trail"].isNull())       n.trail      = constrain(doc["trail"].as<int>(), 1, 100);
  if (!doc["staticColor"].isNull()) parseHex(doc["staticColor"].as<const char*>(), n.staticColor);
  if (!doc["pitchLow"].isNull())    n.pitchLow   = constrain(doc["pitchLow"].as<int>(), 24, 107);
  if (!doc["pitchHigh"].isNull())   n.pitchHigh  = constrain(doc["pitchHigh"].as<int>(), 25, 108);
  if (!doc["reverse"].isNull())     n.reverse    = doc["reverse"].as<bool>();
  if (n.pitchHigh <= n.pitchLow)    n.pitchHigh  = n.pitchLow + 1;

  if (doc["noteColors"].is<JsonArray>()) {
    int i = 0;
    for (JsonVariant v : doc["noteColors"].as<JsonArray>()) {
      if (i >= 12) break;
      parseHex(v.as<const char*>(), n.noteColors[i++]);   // invalid entry: keep old color
    }
  }

  if (!doc["source"].isNull())    n.audioSource = constrain(doc["source"].as<int>(), 0, 1);
  if (!doc["gateDb"].isNull())    n.gateDb      = constrain(doc["gateDb"].as<int>(), -80, -10);
  if (!doc["fullDb"].isNull())    n.fullDb      = constrain(doc["fullDb"].as<int>(), -40, 0);
  if (!doc["releaseMs"].isNull()) n.releaseMs   = constrain(doc["releaseMs"].as<int>(), 20, 1000);
  if (!doc["attackDb"].isNull())  n.attackDb    = constrain(doc["attackDb"].as<int>(), 2, 30);
  if (!doc["pitchConf"].isNull()) n.pitchConf   = constrain(doc["pitchConf"].as<int>(), 50, 99);
  if (n.fullDb < n.gateDb + 6)    n.fullDb      = n.gateDb + 6;   // keep a usable range

  if (doc["test"].is<JsonObject>()) {
    JsonObject t = doc["test"].as<JsonObject>();
    if (!t["auto"].isNull())  n.testAuto  = t["auto"].as<bool>();
    if (!t["level"].isNull()) n.testLevel = constrain(t["level"].as<float>(), 0.0f, 1.0f);
    if (!t["note"].isNull()) {
      const int note = t["note"].as<int>();   // MIDI note, or -1 for none
      n.testNote = (note < 0) ? -1 : (int8_t)constrain(note, 24, 108);
    }
  }

  if (doc["battery"].is<JsonObject>()) {
    JsonObject b = doc["battery"].as<JsonObject>();
    if (!b["fitted"].isNull()) n.batteryFitted = b["fitted"].as<bool>();
    if (!b["trimMv"].isNull()) n.batteryTrimMv = (int16_t)constrain(b["trimMv"].as<int>(), -300, 300);
  }
  commit(n);
  return true;
}
