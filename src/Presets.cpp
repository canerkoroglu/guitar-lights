#include "Presets.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

namespace {

// Note color sets, C to B.
constexpr const char* RAINBOW = R"(["#ff0000","#ff7f00","#ffff00","#7fff00","#00ff00","#00ff7f","#00ffff","#007fff","#0000ff","#7f00ff","#ff00ff","#ff007f"])";
constexpr const char* FIRE    = R"(["#ff0000","#ff2200","#ff4800","#ff6a00","#ff8c00","#ffb200","#ffd400","#ffb200","#ff8c00","#ff6a00","#ff4800","#ff2200"])";
constexpr const char* ICE     = R"(["#00ffff","#00d4ff","#00aaff","#007fff","#0055ff","#002aff","#0000ff","#002aff","#0055ff","#007fff","#00aaff","#00d4ff"])";
constexpr const char* NEON    = R"(["#ff00ff","#00ffff","#aaff00","#ff0080","#00ff80","#ffff00","#8000ff","#00aaff","#ff5500","#ff00aa","#00ff00","#0055ff"])";
constexpr const char* SUNSET  = R"(["#2d0a4f","#541388","#9033a0","#db4ea3","#ff6f91","#ff9c7a","#ffc66e","#ffdf85","#ff9c7a","#ff6f91","#db4ea3","#9033a0"])";
constexpr const char* MINT    = R"(["#003b36","#005b50","#00796b","#26a69a","#4db6ac","#80cbc4","#b2dfdb","#e0f2f1","#b2dfdb","#80cbc4","#4db6ac","#26a69a"])";
constexpr const char* VIOLET  = R"(["#240046","#3c096c","#5a189a","#7b2cbf","#9d4edd","#c77dff","#e0aaff","#ffffff","#e0aaff","#c77dff","#9d4edd","#7b2cbf"])";

struct Preset {
  const char* name;
  EffectMode  mode;
  uint8_t     speed, trail;
  const char* noteColors;    // JSON array of 12 colors, C to B
  const char* staticColor;   // "#rrggbb", or nullptr to leave unchanged
};

const Preset PRESETS[] = {
  {"Classic VU",       MODE_VU,              50, 50, RAINBOW, nullptr},
  {"Rainbow notes",    MODE_PITCH_COLOR,     50, 50, RAINBOW, nullptr},
  {"Melody trail",     MODE_PITCH_POSITION,  50, 85, RAINBOW, nullptr},
  {"Slide",            MODE_PITCH_DIRECTION, 60, 40, NEON,    nullptr},
  {"Blues",            MODE_NOTE_FLOW,       30, 80, ICE,     nullptr},
  {"Stage",            MODE_RIPPLE,          75, 30, NEON,    nullptr},
  {"Metal",            MODE_RIPPLE,          95, 15, FIRE,    nullptr},
  {"Chill",            MODE_BREATHING,       15, 50, ICE,     "#ff8c1a"},
  {"Party",            MODE_RAINBOW,         80, 50, RAINBOW, nullptr},
  {"Campfire",         MODE_FIRE,            50, 70, FIRE,    nullptr},
  {"Night ocean",      MODE_OCEAN,           20, 50, ICE,     nullptr},
  {"Strobe",           MODE_PEAK_FLASH,      50, 50, NEON,    nullptr},
  {"Fireworks",        MODE_SPARKLE,         50, 40, NEON,    nullptr},
  {"Rhythm",           MODE_STEP_CHASE,      50, 50, NEON,    nullptr},
  {"Starry night",     MODE_TWINKLE,         30, 80, VIOLET,  "#b0c8ff"},
  {"Solo note color",  MODE_PITCH_COLOR,     50, 50, RAINBOW, nullptr},
  {"Warm solo",        MODE_PITCH_COLOR,     50, 50, FIRE,    nullptr},
  {"Ice solo",         MODE_PITCH_COLOR,     50, 50, ICE,     nullptr},
  {"Sunset flow",      MODE_NOTE_FLOW,       45, 75, SUNSET,  nullptr},
  {"Mint slide",       MODE_PITCH_DIRECTION, 55, 45, MINT,    nullptr},
  {"Ember ripple",     MODE_RIPPLE,          80, 25, FIRE,    nullptr},
  {"Violet position",  MODE_PITCH_POSITION,  45, 75, VIOLET,  nullptr},
};

constexpr size_t COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);
static_assert(sizeof(RGBColor) == 3, "Palette storage assumes packed RGB bytes");
RGBColor palettes[COUNT][12];
bool palettesReady = false;

bool parseHex(const char* text, RGBColor& out) {
  if (!text || strlen(text) != 7 || text[0] != '#') return false;
  for (int i = 1; i < 7; ++i) if (!isxdigit((unsigned char)text[i])) return false;
  const uint32_t rgb = strtoul(text + 1, nullptr, 16);
  out = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
  return true;
}

bool parsePalette(const char* json, RGBColor out[12]) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json)) return false;
  JsonArrayConst colors = doc.as<JsonArrayConst>();
  if (colors.size() != 12) return false;
  for (size_t i = 0; i < 12; ++i) {
    const char* text = colors[i] | "";
    if (!parseHex(text, out[i])) return false;
  }
  return true;
}

void initializePalettes() {
  if (palettesReady) return;
  for (size_t i = 0; i < COUNT; ++i) parsePalette(PRESETS[i].noteColors, palettes[i]);

  Preferences prefs;
  if (prefs.begin("gtrpal", true)) {
    const size_t bytes = sizeof(palettes);
    if (prefs.getBytesLength("pal") == bytes) prefs.getBytes("pal", palettes, bytes);
    prefs.end();
  }
  palettesReady = true;
}

String colorHex(const RGBColor& color) {
  char text[8];
  snprintf(text, sizeof(text), "#%02x%02x%02x", color.r, color.g, color.b);
  return String(text);
}

String toJson(size_t index) {
  const Preset& p = PRESETS[index];
  String json = String("{\"mode\":") + (int)p.mode + ",\"speed\":" + p.speed + ",\"trail\":" + p.trail + ",\"noteColors\":[";
  for (size_t i = 0; i < 12; ++i) {
    if (i) json += ',';
    json += '"'; json += colorHex(palettes[index][i]); json += '"';
  }
  json += ']';
  if (p.staticColor) json += String(",\"staticColor\":\"") + p.staticColor + '"';
  return json + '}';
}

}  // namespace

size_t Presets::count() { return COUNT; }

const char* Presets::name(size_t i) { return i < COUNT ? PRESETS[i].name : ""; }

void Presets::getPalette(size_t i, RGBColor out[12]) {
  if (!out || i >= COUNT) return;
  initializePalettes();
  memcpy(out, palettes[i], sizeof(palettes[i]));
}

bool Presets::setPalette(size_t i, const RGBColor colors[12]) {
  if (i >= COUNT || !colors) return false;
  initializePalettes();
  RGBColor previous[12];
  memcpy(previous, palettes[i], sizeof(previous));
  memcpy(palettes[i], colors, sizeof(palettes[i]));

  Preferences prefs;
  if (!prefs.begin("gtrpal", false)) {
    memcpy(palettes[i], previous, sizeof(previous));
    return false;
  }
  const size_t written = prefs.putBytes("pal", palettes, sizeof(palettes));
  prefs.end();
  if (written != sizeof(palettes)) {
    memcpy(palettes[i], previous, sizeof(previous));
    return false;
  }
  return true;
}

bool Presets::parseIndex(const char* text, size_t& i) {
  if (!text || !isdigit((unsigned char)*text)) return false;
  char* end = nullptr;
  const unsigned long value = strtoul(text, &end, 10);
  if (*end != '\0' || value >= COUNT) return false;
  i = (size_t)value;
  return true;
}

bool Presets::apply(size_t i) {
  if (i >= COUNT) return false;
  initializePalettes();
  String err;
  if (!ConfigStore::fromJson(toJson(i), err)) return false;
  ConfigStore::requestSave();
  return true;
}
