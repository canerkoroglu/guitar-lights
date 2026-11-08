#include "EffectCatalog.h"

namespace {

constexpr const char* AMBIENT = "Ambient";
constexpr const char* WHOLE   = "Whole strip (audio)";
constexpr const char* MOVING  = "Moving along strip (audio)";

const EffectCatalog::Info EFFECTS[] = {
  {MODE_STATIC,          AMBIENT, "Static color",    "One color on every LED.", "c"},
  {MODE_RAINBOW,         AMBIENT, "Rainbow",         "Rainbow scrolling along the strip.", "s"},
  {MODE_BREATHING,       AMBIENT, "Breathing",       "Static color slowly fading in and out.", "sc"},
  {MODE_TWINKLE,         AMBIENT, "Twinkle",         "Random LEDs light in the static color and fade, like stars. Speed: how many. Trail: how long they glow.", "stc"},
  {MODE_SCANNER,         AMBIENT, "Scanner",         "One dot in the static color sweeps end to end with a fading tail.", "stc"},
  {MODE_OCEAN,           AMBIENT, "Ocean",           "Blues and greens drifting slowly along the strip.", "s"},
  {MODE_LAVA,            AMBIENT, "Lava",            "Reds and oranges drifting slowly along the strip.", "s"},

  {MODE_VU,              WHOLE,   "VU meter",        "Number of lit LEDs follows how hard you play.", ""},
  {MODE_PITCH_COLOR,     WHOLE,   "Solo note color (all LEDs)", "All LEDs show the mapped color of the note being played. Edit the palette in Note → color.", ""},
  {MODE_CENTER_VU,       WHOLE,   "Center VU",       "Level bar grows from the middle out to both ends.", ""},
  {MODE_PEAK_FLASH,      WHOLE,   "Peak flash",      "VU meter, plus a white flash on every pick.", ""},
  {MODE_SUSTAIN,         WHOLE,   "Sustain",         "Whole strip in the note color: bright on a pick, follows the ringing note, then fades. Trail: fade time.", "tc"},
  {MODE_FIRE,            WHOLE,   "Fire",            "Flames rise from the start of the strip; playing and picks feed the fire. Trail: flame height.", "t"},

  {MODE_PITCH_POSITION,  MOVING,  "Pitch position",  "A dot sits where the note falls between the lowest and highest note, leaving a trail as you move.", "t"},
  {MODE_PITCH_DIRECTION, MOVING,  "Pitch direction", "Bands travel up the strip as pitch rises and down as it falls. Holds still on a steady note.", "st"},
  {MODE_NOTE_FLOW,       MOVING,  "Note flow",       "Each note's color enters at one end and scrolls along, showing what you just played.", "st"},
  {MODE_RIPPLE,          MOVING,  "Ripple",          "Each pick sends pulses outward from the note's position.", "st"},
  {MODE_SPARKLE,         MOVING,  "Sparkle",         "Each pick lights random LEDs in the note color; harder picks, more sparks.", "tc"},
  {MODE_COMET,           MOVING,  "Comet",           "Each pick launches a comet in the note color; harder picks fly faster.", "stc"},
  {MODE_STEP_CHASE,      MOVING,  "Step chase",      "A block of 4 LEDs steps along the strip on every pick.", "tc"},
};

constexpr size_t COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);
static_assert(COUNT == MODE_COUNT, "Every EffectMode needs exactly one EffectCatalog entry");

}  // namespace

size_t EffectCatalog::count() { return COUNT; }

const EffectCatalog::Info& EffectCatalog::at(size_t i) { return EFFECTS[i < COUNT ? i : 0]; }

bool EffectCatalog::selfCheck() {
  bool seen[MODE_COUNT] = {};
  for (const Info& e : EFFECTS) {
    if (e.mode >= MODE_COUNT || seen[e.mode]) return false;
    seen[e.mode] = true;
  }
  return true;   // COUNT == MODE_COUNT and no duplicates: all present
}
