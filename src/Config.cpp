#include "Config.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Settings    g;
static Preferences prefs;

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

static RGBColor fromHex(const char* s) {
  RGBColor c{0, 0, 0};
  if (!s) return c;
  if (*s == '#') s++;
  long v = strtol(s, nullptr, 16);
  c.r = (v >> 16) & 0xFF;
  c.g = (v >> 8) & 0xFF;
  c.b = v & 0xFF;
  return c;
}

void ConfigStore::setDefaults() {
  g.power       = true;
  g.brightness  = 64;
  g.mode        = MODE_RAINBOW;
  g.speed       = 50;
  g.trail       = 50;
  g.staticColor = {255, 0, 0};
  g.pitchLow    = 40;   // E2, lowest open string
  g.pitchHigh   = 76;   // E5 (readme.md Phase 4 target range)
  g.reverse     = false;

  // 12-step hue wheel, C = red ... B = rose (readme.md "Pitch Color" example).
  static const uint32_t pal[12] = {
    0xff0000, 0xff7f00, 0xffff00, 0x7fff00, 0x00ff00, 0x00ff7f,
    0x00ffff, 0x007fff, 0x0000ff, 0x7f00ff, 0xff00ff, 0xff007f
  };
  for (int i = 0; i < 12; i++) {
    g.noteColors[i] = {(uint8_t)(pal[i] >> 16), (uint8_t)(pal[i] >> 8), (uint8_t)pal[i]};
  }

  g.audioSource = SOURCE_SIMULATED;   // no input circuit yet
  g.gateDb      = -45;
  g.fullDb      = -6;
  g.releaseMs   = 120;
  g.attackDb    = 6;

  g.testAuto  = true;
  g.testLevel = 0.7f;
  g.testNote  = -1;
}

Settings& ConfigStore::get() { return g; }

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

  JsonObject t = doc["test"].to<JsonObject>();
  t["auto"]  = g.testAuto;
  t["level"] = g.testLevel;
  t["note"]  = g.testNote;

  String out;
  serializeJson(doc, out);
  return out;
}

bool ConfigStore::fromJson(const String& body, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) { err = e.c_str(); return false; }

  // Partial updates are allowed: only keys present in the body change.
  if (!doc["power"].isNull())       g.power      = doc["power"].as<bool>();
  if (!doc["brightness"].isNull())  g.brightness = constrain(doc["brightness"].as<int>(), 0, 255);
  if (!doc["mode"].isNull())        g.mode       = constrain(doc["mode"].as<int>(), 0, (int)MODE_COUNT - 1);
  if (!doc["speed"].isNull())       g.speed      = constrain(doc["speed"].as<int>(), 1, 100);
  if (!doc["trail"].isNull())       g.trail      = constrain(doc["trail"].as<int>(), 1, 100);
  if (!doc["staticColor"].isNull()) g.staticColor = fromHex(doc["staticColor"].as<const char*>());
  if (!doc["pitchLow"].isNull())    g.pitchLow   = constrain(doc["pitchLow"].as<int>(), 24, 107);
  if (!doc["pitchHigh"].isNull())   g.pitchHigh  = constrain(doc["pitchHigh"].as<int>(), 25, 108);
  if (!doc["reverse"].isNull())     g.reverse    = doc["reverse"].as<bool>();
  if (g.pitchHigh <= g.pitchLow)    g.pitchHigh  = g.pitchLow + 1;

  if (doc["noteColors"].is<JsonArray>()) {
    int i = 0;
    for (JsonVariant v : doc["noteColors"].as<JsonArray>()) {
      if (i >= 12) break;
      g.noteColors[i++] = fromHex(v.as<const char*>());
    }
  }

  if (!doc["source"].isNull())    g.audioSource = constrain(doc["source"].as<int>(), 0, 1);
  if (!doc["gateDb"].isNull())    g.gateDb      = constrain(doc["gateDb"].as<int>(), -80, -10);
  if (!doc["fullDb"].isNull())    g.fullDb      = constrain(doc["fullDb"].as<int>(), -40, 0);
  if (!doc["releaseMs"].isNull()) g.releaseMs   = constrain(doc["releaseMs"].as<int>(), 20, 1000);
  if (!doc["attackDb"].isNull())  g.attackDb    = constrain(doc["attackDb"].as<int>(), 2, 30);
  if (g.fullDb < g.gateDb + 6)    g.fullDb      = g.gateDb + 6;   // keep a usable range

  if (doc["test"].is<JsonObject>()) {
    JsonObject t = doc["test"].as<JsonObject>();
    if (!t["auto"].isNull())  g.testAuto  = t["auto"].as<bool>();
    if (!t["level"].isNull()) g.testLevel = constrain(t["level"].as<float>(), 0.0f, 1.0f);
    if (!t["note"].isNull()) {
      int n = t["note"].as<int>();   // MIDI note, or -1 for none
      g.testNote = (n < 0) ? -1 : (int8_t)constrain(n, 24, 108);
    }
  }
  return true;
}
