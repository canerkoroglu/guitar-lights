/*
 * EffectEngine.h — renders LED frames from Settings + AudioFeatures.
 *
 * The engine never reads the ADC. It consumes AudioFeatures from AudioDsp.
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
  struct Comet {
    float pos, speed;
    CRGB  color;
    bool  active;
  };
  static constexpr int MAX_RIPPLES = 6;
  static constexpr int MAX_COMETS  = 4;
  static constexpr int MAX_LEDS    = 64;   // Fire heat buffer

  uint16_t idx(const Settings& s, int i) const;               // logical -> physical (reverse)
  float    pitchPos(const Settings& s, float midi) const;     // 0..n-1
  void     dotAt(const Settings& s, float pos, const CRGB& c);
  void     drawVu(const Settings& s, float level);            // VU meter and Peak flash
  void     recolorRecent(const Settings& s, const AudioFeatures& a);

  void renderPitchPosition(const Settings& s, const AudioFeatures& a);
  void renderPitchDirection(const Settings& s, const AudioFeatures& a, float dt);
  void renderNoteFlow(const Settings& s, const AudioFeatures& a, float dt);
  void renderRipple(const Settings& s, const AudioFeatures& a, float dt);
  void renderTwinkle(const Settings& s, float dt);
  void renderScanner(const Settings& s, float dt);
  void renderPalette(const Settings& s, const TProgmemRGBPalette16& pal, uint32_t now, float dt);
  void renderPeakFlash(const Settings& s, const AudioFeatures& a, uint32_t now);
  void renderSustain(const Settings& s, const AudioFeatures& a, float dt);
  void renderCenterVu(const AudioFeatures& a);
  void renderFire(const Settings& s, const AudioFeatures& a);
  void renderSparkle(const Settings& s, const AudioFeatures& a);
  void renderComet(const Settings& s, const AudioFeatures& a, float dt);
  void renderStepChase(const Settings& s, const AudioFeatures& a, uint32_t now);

  CRGB*    leds_    = nullptr;
  uint16_t n_       = 0;
  uint32_t lastNow_ = 0;

  // Moving effects keep state between frames.
  float    phase_   = 0.0f;   // Pitch direction wave offset, in LEDs
  float    flowAcc_ = 0.0f;   // Note flow fractional step accumulator
  Ripple   ripples_[MAX_RIPPLES] = {};
  float    twinkleAcc_ = 0.0f, scanPhase_ = 0.0f, paletteOffset_ = 0.0f;
  float    flashPeak_ = 0.0f;
  uint32_t flashStart_ = 0;
  CRGB     flashColor_ = CRGB::White;
  float    sustain_ = 0.0f;
  CRGB     sustainColor_ = CRGB::Black;
  uint8_t  heat_[MAX_LEDS] = {};
  Comet    comets_[MAX_COMETS] = {};
  float    stepFrom_ = 0.0f, stepTarget_ = 0.0f;
  uint32_t stepStart_ = 0;

  // Late note: pitch is confirmed a little after the attack that starts it.
  int      youngestRipple_ = -1, youngestComet_ = -1, lastNote_ = -1;
  uint32_t attackAt_ = 0;
};
