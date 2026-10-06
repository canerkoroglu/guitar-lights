#include "EffectEngine.h"
#include <math.h>

static inline CRGB toCRGB(const RGBColor& c) { return CRGB(c.r, c.g, c.b); }

static inline CRGB scaled(CRGB c, float k) {
  c.nscale8_video((uint8_t)(constrain(k, 0.0f, 1.0f) * 255.0f));
  return c;
}

static inline int pitchClass(float midi) { return ((int)lroundf(midi) % 12 + 12) % 12; }

static inline CRGB noteColor(const Settings& s, float midi) {
  return toCRGB(s.noteColors[pitchClass(midi)]);
}

// Trail 1..100 -> per-frame fade amount (long trail = gentle fade).
static inline uint8_t trailFade(const Settings& s) {
  return (uint8_t)map(s.trail, 1, 100, 90, 3);
}

uint16_t EffectEngine::idx(const Settings& s, int i) const {
  return s.reverse ? (uint16_t)(n_ - 1 - i) : (uint16_t)i;
}

float EffectEngine::pitchPos(const Settings& s, float midi) const {
  float t = (midi - s.pitchLow) / (float)(s.pitchHigh - s.pitchLow);
  return constrain(t, 0.0f, 1.0f) * (n_ - 1);
}

// Dot at a fractional position, split across the two nearest LEDs so
// movement looks smooth. Per-channel max (|=) rather than add, so a dot
// redrawn every frame over its own trail does not saturate toward white.
void EffectEngine::dotAt(const Settings& s, float pos, const CRGB& c) {
  if (pos < 0.0f || pos > (float)(n_ - 1)) return;
  int   i0   = (int)floorf(pos);
  float frac = pos - i0;
  leds_[idx(s, i0)] |= scaled(c, 1.0f - frac);
  if (i0 + 1 < n_) leds_[idx(s, i0 + 1)] |= scaled(c, frac);
}

void EffectEngine::render(const Settings& s, uint32_t now, const AudioFeatures& a) {
  if (!leds_ || !n_) return;

  float dt = lastNow_ ? (now - lastNow_) / 1000.0f : 0.016f;
  if (dt <= 0.0f || dt > 0.25f) dt = 0.016f;
  lastNow_ = now;

  if (!s.power) {
    fill_solid(leds_, n_, CRGB::Black);
    return;
  }

  const uint32_t spd = s.speed;   // 1..100

  switch (s.mode) {
    case MODE_STATIC:
      fill_solid(leds_, n_, toCRGB(s.staticColor));
      break;

    case MODE_RAINBOW: {
      uint8_t hue = (uint8_t)((now * (spd + 1)) / 40);
      fill_rainbow(leds_, n_, hue, max(1, 255 / n_));
      break;
    }

    case MODE_BREATHING: {
      uint8_t b = sin8((uint8_t)((now * (spd + 1)) / 30));
      CRGB c = toCRGB(s.staticColor);
      c.nscale8_video(b);
      fill_solid(leds_, n_, c);
      break;
    }

    case MODE_VU: {
      // Lit LED count follows level; green at the bottom -> red at the top.
      int lit = (int)roundf(constrain(a.level, 0.0f, 1.0f) * n_);
      long top = (n_ > 1) ? n_ - 1 : 1;
      for (uint16_t i = 0; i < n_; i++) {
        if ((int)i < lit) leds_[idx(s, i)] = CHSV((uint8_t)map(i, 0, top, 96, 0), 255, 255);
        else              leds_[idx(s, i)] = CRGB::Black;
      }
      break;
    }

    case MODE_PITCH_COLOR:
      if (a.pitchValid) fill_solid(leds_, n_, noteColor(s, a.midiNote));
      else              fill_solid(leds_, n_, CRGB(8, 8, 8));  // idle glow: no note
      break;

    case MODE_PITCH_POSITION:  renderPitchPosition(s, a);      break;
    case MODE_PITCH_DIRECTION: renderPitchDirection(s, a, dt); break;
    case MODE_NOTE_FLOW:       renderNoteFlow(s, a, dt);       break;
    case MODE_RIPPLE:          renderRipple(s, a, dt);         break;

    default:
      fill_solid(leds_, n_, CRGB::Black);
      break;
  }
}

// Pitch position (readme.md Effect 4): a dot sits where the note falls
// between pitchLow and pitchHigh, in the note's color, leaving a fading
// trail as the pitch moves.
void EffectEngine::renderPitchPosition(const Settings& s, const AudioFeatures& a) {
  fadeToBlackBy(leds_, n_, trailFade(s));
  if (!a.pitchValid) return;
  dotAt(s, pitchPos(s, a.midiNote), scaled(noteColor(s, a.midiNote), 0.25f + 0.75f * a.level));
}

// Pitch direction (readme.md Effect 5): bands in the note's color travel
// up the strip while pitch rises and down while it falls. A deadband keeps
// the bands still on a steady note, so vibrato does not read as movement.
void EffectEngine::renderPitchDirection(const Settings& s, const AudioFeatures& a, float dt) {
  static constexpr float DEADBAND = 1.5f;   // semitones/s treated as "steady"
  static constexpr float WAVELEN  = 6.0f;   // LEDs per band

  if (!a.pitchValid) {
    fadeToBlackBy(leds_, n_, trailFade(s));
    return;
  }

  float v = a.pitchVelocity;
  if (fabsf(v) < DEADBAND) v = 0.0f;

  // Band speed follows pitch velocity (speed 50 -> 2 LEDs per semitone) but
  // is capped. A note jump reads as hundreds of semitones/s; uncapped, the
  // bands would move several LEDs per frame and appear to run backwards
  // (wagon-wheel aliasing). Cap stays well under WAVELEN/2 per frame.
  const float maxSpeed  = 6.0f + s.speed * 0.3f;   // LEDs/s
  const float bandSpeed = constrain(v * (s.speed / 25.0f), -maxSpeed, maxSpeed);
  phase_ += bandSpeed * dt;
  phase_  = fmodf(phase_, WAVELEN * 1000.0f);      // bound it, stay a whole number of bands

  CRGB  base = noteColor(s, a.midiNote);
  float k    = 0.15f + 0.85f * a.level;
  for (uint16_t i = 0; i < n_; i++) {
    int32_t theta = (int32_t)lroundf((i - phase_) / WAVELEN * 256.0f);
    float   w     = sin8((uint8_t)(theta & 0xFF)) / 255.0f;
    leds_[idx(s, i)] = scaled(base, k * w * w);   // squared: sharper bands
  }
}

// Note flow: the current note's color is fed in at the first LED and
// scrolls along the strip, so it shows a running history of what was
// played. Brightness follows the envelope, so each pick is a bright band
// that dims as the note decays. Trail sets how far the history survives.
void EffectEngine::renderNoteFlow(const Settings& s, const AudioFeatures& a, float dt) {
  fadeToBlackBy(leds_, n_, trailFade(s) / 6);

  CRGB in = a.pitchValid ? scaled(noteColor(s, a.midiNote), a.level) : CRGB::Black;

  flowAcc_ += dt * (2.0f + s.speed * 0.38f);   // speed 1..100 -> ~2..40 LEDs/s
  while (flowAcc_ >= 1.0f) {
    for (int i = n_ - 1; i > 0; i--) leds_[idx(s, i)] = leds_[idx(s, i - 1)];
    flowAcc_ -= 1.0f;
  }
  leds_[idx(s, 0)] = in;   // entry LED is always live, so attacks show at once
}

// Ripple: every pick launches a pair of pulses from the note's position,
// moving outward in the note's color and fading as they spread.
void EffectEngine::renderRipple(const Settings& s, const AudioFeatures& a, float dt) {
  fadeToBlackBy(leds_, n_, trailFade(s));

  if (a.attackDetected) {
    int slot = 0;   // first free slot, otherwise the oldest (largest) ripple
    for (int r = 0; r < MAX_RIPPLES; r++) {
      if (!ripples_[r].active) { slot = r; break; }
      if (ripples_[r].radius > ripples_[slot].radius) slot = r;
    }
    float center = a.pitchValid ? pitchPos(s, a.midiNote) : (n_ - 1) / 2.0f;
    CRGB  color  = a.pitchValid ? noteColor(s, a.midiNote) : CRGB::White;
    ripples_[slot] = {center, 0.0f, fmaxf(0.3f, a.attackStrength), color, true};
  }

  const float maxR  = (float)n_;
  const float speed = 4.0f + s.speed * 0.3f;   // LEDs per second
  for (Ripple& r : ripples_) {
    if (!r.active) continue;
    r.radius += speed * dt;
    float k = r.strength * (1.0f - r.radius / maxR);
    if (k <= 0.0f) { r.active = false; continue; }
    CRGB c = scaled(r.color, k);
    dotAt(s, r.center + r.radius, c);
    dotAt(s, r.center - r.radius, c);
  }
}
