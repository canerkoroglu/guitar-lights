/*
 * EffectEngine.h — renders LED frames from Settings + AudioFeatures.
 *
 * The engine never reads the ADC. It consumes AudioFeatures, which come
 * from AudioSim today and from the real DSP layer in Phase 2/4.
 */
#pragma once
#include <FastLED.h>
#include "Config.h"
#include "AudioFeatures.h"

class EffectEngine {
public:
  void begin(CRGB* leds, uint16_t n) { leds_ = leds; n_ = n; }
  void render(const Settings& s, uint32_t now, const AudioFeatures& a);

private:
  struct Ripple {
    float center, radius, strength;
    CRGB  color;
    bool  active;
  };
  static constexpr int MAX_RIPPLES = 6;

  uint16_t idx(const Settings& s, int i) const;               // logical -> physical (reverse)
  float    pitchPos(const Settings& s, float midi) const;     // 0..n-1
  void     dotAt(const Settings& s, float pos, const CRGB& c);

  void renderPitchPosition(const Settings& s, const AudioFeatures& a);
  void renderPitchDirection(const Settings& s, const AudioFeatures& a, float dt);
  void renderNoteFlow(const Settings& s, const AudioFeatures& a, float dt);
  void renderRipple(const Settings& s, const AudioFeatures& a, float dt);

  CRGB*    leds_    = nullptr;
  uint16_t n_       = 0;
  uint32_t lastNow_ = 0;

  // Moving effects keep state between frames.
  float  phase_   = 0.0f;   // Pitch direction wave offset, in LEDs
  float  flowAcc_ = 0.0f;   // Note flow fractional step accumulator
  Ripple ripples_[MAX_RIPPLES] = {};
};
