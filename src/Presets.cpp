#include "Presets.h"
#include "Config.h"

// Note color sets, C to B.
#define RAINBOW R"(["#ff0000","#ff7f00","#ffff00","#7fff00","#00ff00","#00ff7f","#00ffff","#007fff","#0000ff","#7f00ff","#ff00ff","#ff007f"])"
#define FIRE    R"(["#ff0000","#ff2200","#ff4800","#ff6a00","#ff8c00","#ffb200","#ffd400","#ffb200","#ff8c00","#ff6a00","#ff4800","#ff2200"])"
#define ICE     R"(["#00ffff","#00d4ff","#00aaff","#007fff","#0055ff","#002aff","#0000ff","#002aff","#0055ff","#007fff","#00aaff","#00d4ff"])"
#define NEON    R"(["#ff00ff","#00ffff","#aaff00","#ff0080","#00ff80","#ffff00","#8000ff","#00aaff","#ff5500","#ff00aa","#00ff00","#0055ff"])"

namespace {

struct Preset {
  const char* name;
  const char* json;   // only effect, speed, trail, static color, note colors
};

// Effect numbers are EffectMode values (Config.h).
const Preset PRESETS[] = {
  {"Classic VU",    R"({"mode":3,"speed":50,"trail":50,"noteColors":)" RAINBOW "}"},
  {"Rainbow notes", R"({"mode":4,"speed":50,"trail":50,"noteColors":)" RAINBOW "}"},
  {"Melody trail",  R"({"mode":5,"speed":50,"trail":85,"noteColors":)" RAINBOW "}"},
  {"Slide",         R"({"mode":6,"speed":60,"trail":40,"noteColors":)" NEON "}"},
  {"Blues",         R"({"mode":7,"speed":30,"trail":80,"noteColors":)" ICE "}"},
  {"Stage",         R"({"mode":8,"speed":75,"trail":30,"noteColors":)" NEON "}"},
  {"Metal",         R"({"mode":8,"speed":95,"trail":15,"noteColors":)" FIRE "}"},
  {"Chill",         R"({"mode":2,"speed":15,"trail":50,"staticColor":"#ff8c1a"})"},
  {"Party",         R"({"mode":1,"speed":80,"trail":50})"},
  {"Campfire",      R"({"mode":16,"speed":50,"trail":70})"},
  {"Night ocean",   R"({"mode":11,"speed":20,"trail":50})"},
  {"Strobe",        R"({"mode":13,"speed":50,"trail":50,"noteColors":)" NEON "}"},
  {"Fireworks",     R"({"mode":17,"speed":50,"trail":40,"noteColors":)" NEON "}"},
  {"Rhythm",        R"({"mode":19,"speed":50,"trail":50,"noteColors":)" NEON "}"},
  {"Starry night",  R"({"mode":9,"speed":30,"trail":80,"staticColor":"#b0c8ff"})"},
};

constexpr size_t COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

}  // namespace

size_t Presets::count() { return COUNT; }

const char* Presets::name(size_t i) { return i < COUNT ? PRESETS[i].name : ""; }

bool Presets::apply(size_t i) {
  if (i >= COUNT) return false;
  String err;
  if (!ConfigStore::fromJson(PRESETS[i].json, err)) return false;
  ConfigStore::requestSave();
  return true;
}
