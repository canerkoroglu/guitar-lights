/*
 * Config.h — central settings model + persistence (NVS) + JSON.
 *
 * All user-tunable state lives in one Settings struct so the web portal,
 * the effect engine, and persistence agree on a single source of truth.
 */
#pragma once
#include <Arduino.h>

// Effect modes. Saved settings store the number, so append new modes at the
// end. Keep in sync with the <select> in WebPortal.cpp and the switch() in
// EffectEngine.cpp.
enum EffectMode : uint8_t {
  // Ambient: no audio needed
  MODE_STATIC = 0,
  MODE_RAINBOW,
  MODE_BREATHING,
  // Audio: whole strip changes together
  MODE_VU,
  MODE_PITCH_COLOR,
  // Audio: light moves along the strip
  MODE_PITCH_POSITION,
  MODE_PITCH_DIRECTION,
  MODE_NOTE_FLOW,
  MODE_RIPPLE,
  // Added later, so numbers stay stable. Groups in the web UI:
  MODE_TWINKLE,        //  9 ambient
  MODE_SCANNER,        // 10 ambient
  MODE_OCEAN,          // 11 ambient
  MODE_LAVA,           // 12 ambient
  MODE_PEAK_FLASH,     // 13 whole strip (readme Effect 2)
  MODE_SUSTAIN,        // 14 whole strip (readme Effect 7)
  MODE_CENTER_VU,      // 15 whole strip
  MODE_FIRE,           // 16 whole strip
  MODE_SPARKLE,        // 17 moving
  MODE_COMET,          // 18 moving
  MODE_STEP_CHASE,     // 19 moving (readme "rhythm response")
  MODE_COUNT
};

struct RGBColor { uint8_t r, g, b; };

struct Settings {
  bool     power;              // master on/off
  uint8_t  brightness;         // 0..255
  uint8_t  mode;               // EffectMode
  uint8_t  speed;              // 1..100 animation / movement speed
  uint8_t  trail;              // 1..100 trail length for moving effects
  RGBColor staticColor;        // Static / Breathing base color
  RGBColor noteColors[12];     // pitch-class C..B -> color

  // Pitch -> strip position (Pitch position, Ripple)
  uint8_t  pitchLow;           // MIDI note at the first LED (default E2 = 40)
  uint8_t  pitchHigh;          // MIDI note at the last LED  (default E5 = 76)
  bool     reverse;            // mirror moving effects to match how the strip is mounted

  // Audio analysis (Phase 2 VU level, Phase 3 attacks)
  uint8_t  audioSource;        // AudioSource
  int8_t   gateDb;             // quieter than this (dB re full scale) = silence, level 0
  int8_t   fullDb;             // this loud lights every LED (dB re full scale)
  uint16_t releaseMs;          // how fast the level falls after playing stops
  uint8_t  attackDb;           // attack threshold: level rise within 12 ms (dB)
  uint8_t  pitchConf;          // Phase 4: minimum pitch confidence, % (YIN threshold = 1 - this)

  // Simulated guitar (used when audioSource = SOURCE_SIMULATED)
  bool     testAuto;           // auto-play
  float    testLevel;          // 0..1 pick strength for manual notes and Pluck
  int8_t   testNote;           // -1 = none, else MIDI note (45 = A2)
};

enum AudioSource : uint8_t {
  SOURCE_SIMULATED = 0,        // GuitarSynth, no hardware needed
  SOURCE_GUITAR    = 1,        // ADC on GPIO34 through the MCP6022 buffer
};

namespace ConfigStore {
  void      begin();                               // load from NVS (or defaults)
  void      save();                                // persist current settings now
  void      requestSave();                         // persist once changes stop for 2 s
  void      loop();                                // call from loop(): runs the deferred save
  void      setDefaults();                         // reset in-memory to defaults
  Settings& get();                                 // live reference
  String    toJson();                              // serialize current settings
  bool      fromJson(const String& body, String& err); // merge-update from JSON
}
