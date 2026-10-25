#include "Presets.h"
#include "Config.h"

namespace {

// Note color sets, C to B.
constexpr const char* RAINBOW = R"(["#ff0000","#ff7f00","#ffff00","#7fff00","#00ff00","#00ff7f","#00ffff","#007fff","#0000ff","#7f00ff","#ff00ff","#ff007f"])";
constexpr const char* FIRE    = R"(["#ff0000","#ff2200","#ff4800","#ff6a00","#ff8c00","#ffb200","#ffd400","#ffb200","#ff8c00","#ff6a00","#ff4800","#ff2200"])";
constexpr const char* ICE     = R"(["#00ffff","#00d4ff","#00aaff","#007fff","#0055ff","#002aff","#0000ff","#002aff","#0055ff","#007fff","#00aaff","#00d4ff"])";
constexpr const char* NEON    = R"(["#ff00ff","#00ffff","#aaff00","#ff0080","#00ff80","#ffff00","#8000ff","#00aaff","#ff5500","#ff00aa","#00ff00","#0055ff"])";

struct Preset {
  const char* name;
  EffectMode  mode;
  uint8_t     speed, trail;
  const char* noteColors;    // JSON array of 12 colors, or nullptr to leave unchanged
  const char* staticColor;   // "#rrggbb", or nullptr to leave unchanged
};

const Preset PRESETS[] = {
  {"Classic VU",    MODE_VU,              50, 50, RAINBOW, nullptr},
  {"Rainbow notes", MODE_PITCH_COLOR,     50, 50, RAINBOW, nullptr},
  {"Melody trail",  MODE_PITCH_POSITION,  50, 85, RAINBOW, nullptr},
  {"Slide",         MODE_PITCH_DIRECTION, 60, 40, NEON,    nullptr},
  {"Blues",         MODE_NOTE_FLOW,       30, 80, ICE,     nullptr},
  {"Stage",         MODE_RIPPLE,          75, 30, NEON,    nullptr},
  {"Metal",         MODE_RIPPLE,          95, 15, FIRE,    nullptr},
  {"Chill",         MODE_BREATHING,       15, 50, nullptr, "#ff8c1a"},
  {"Party",         MODE_RAINBOW,         80, 50, nullptr, nullptr},
  {"Campfire",      MODE_FIRE,            50, 70, nullptr, nullptr},
  {"Night ocean",   MODE_OCEAN,           20, 50, nullptr, nullptr},
  {"Strobe",        MODE_PEAK_FLASH,      50, 50, NEON,    nullptr},
  {"Fireworks",     MODE_SPARKLE,         50, 40, NEON,    nullptr},
  {"Rhythm",        MODE_STEP_CHASE,      50, 50, NEON,    nullptr},
  {"Starry night",  MODE_TWINKLE,         30, 80, nullptr, "#b0c8ff"},
};

// Only the fields a preset owns (see Presets.h).
String toJson(const Preset& p) {
  String json = String("{\"mode\":") + (int)p.mode + ",\"speed\":" + p.speed + ",\"trail\":" + p.trail;
  if (p.noteColors)  json += String(",\"noteColors\":") + p.noteColors;
  if (p.staticColor) json += String(",\"staticColor\":\"") + p.staticColor + "\"";
  return json + "}";
}

constexpr size_t COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace

size_t Presets::count() { return COUNT; }

const char* Presets::name(size_t i) { return i < COUNT ? PRESETS[i].name : ""; }

bool Presets::parseIndex(const char* text, size_t& i) {
  if (!text || !isdigit((unsigned char)*text)) return false;
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 10);
  if (*end != '\0') return false;
  i = (size_t)v;
  return true;
}

bool Presets::apply(size_t i) {
  if (i >= COUNT) return false;
  String err;
  if (!ConfigStore::fromJson(toJson(PRESETS[i]), err)) return false;
  ConfigStore::requestSave();
  return true;
}
