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

// Note color while a pitch is known, otherwise the static color. (Until
// Phase 4, pitch only exists with the simulated guitar.)
static inline CRGB noteOrStatic(const Settings& s, const AudioFeatures& a) {
  return a.pitchValid ? noteColor(s, a.midiNote) : toCRGB(s.staticColor);
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

// VU bar: lit LED count follows level; green at the start -> red at the end.
void EffectEngine::drawVu(const Settings& s, float level) {
  int lit = (int)roundf(constrain(level, 0.0f, 1.0f) * n_);
  long top = (n_ > 1) ? n_ - 1 : 1;
  for (uint16_t i = 0; i < n_; i++) {
    if ((int)i < lit) leds_[idx(s, i)] = CHSV((uint8_t)map(i, 0, top, 96, 0), 255, 255);
    else              leds_[idx(s, i)] = CRGB::Black;
  }
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

    case MODE_VU:
      drawVu(s, a.level);
      break;

    case MODE_PITCH_COLOR:
      if (a.pitchValid) fill_solid(leds_, n_, noteColor(s, a.midiNote));
      else              fill_solid(leds_, n_, CRGB(8, 8, 8));  // idle glow: no note
      break;

    case MODE_PITCH_POSITION:  renderPitchPosition(s, a);      break;
    case MODE_PITCH_DIRECTION: renderPitchDirection(s, a, dt); break;
    case MODE_NOTE_FLOW:       renderNoteFlow(s, a, dt);       break;
    case MODE_RIPPLE:          renderRipple(s, a, dt);         break;

    case MODE_TWINKLE:         renderTwinkle(s, dt);                     break;
    case MODE_SCANNER:         renderScanner(s, dt);                     break;
    case MODE_OCEAN:           renderPalette(s, OceanColors_p, now, dt); break;
    case MODE_LAVA:            renderPalette(s, LavaColors_p, now, dt);  break;
    case MODE_PEAK_FLASH:      renderPeakFlash(s, a, now);               break;
    case MODE_SUSTAIN:         renderSustain(s, a, dt);                  break;
    case MODE_CENTER_VU:       renderCenterVu(a);                        break;
    case MODE_FIRE:            renderFire(s, a);                         break;
    case MODE_SPARKLE:         renderSparkle(s, a);                      break;
    case MODE_COMET:           renderComet(s, a, dt);                    break;
    case MODE_STEP_CHASE:      renderStepChase(s, a, now);               break;

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

// Twinkle: random LEDs light in the static color, randomly blended toward
// white, and fade. Speed = stars per second (~1-21), Trail = glow time.
void EffectEngine::renderTwinkle(const Settings& s, float dt) {
  fadeToBlackBy(leds_, n_, (uint8_t)max(1, trailFade(s) / 3));
  twinkleAcc_ += dt * (1.0f + s.speed * 0.2f);
  const CRGB base = toCRGB(s.staticColor);
  while (twinkleAcc_ >= 1.0f) {
    twinkleAcc_ -= 1.0f;
    leds_[random16(n_)] = blend(base, CRGB::White, random8(160));
  }
}

// Scanner: one smooth dot in the static color bounces end to end, easing
// at the ends, with a fading tail. Speed = sweep rate, Trail = tail length.
void EffectEngine::renderScanner(const Settings& s, float dt) {
  fadeToBlackBy(leds_, n_, trailFade(s));
  scanPhase_ = fmodf(scanPhase_ + dt * (0.2f + s.speed * 0.03f), 2.0f);   // sweeps per second
  const float p   = scanPhase_ < 1.0f ? scanPhase_ : 2.0f - scanPhase_;    // 0 -> 1 -> 0
  const float pos = (1.0f - cosf((float)M_PI * p)) * 0.5f * (n_ - 1);
  dotAt(s, pos, toCRGB(s.staticColor));
}

// Ocean / Lava: a palette gradient drifting along the strip with a slow
// brightness shimmer. Speed = drift rate.
void EffectEngine::renderPalette(const Settings& s, const TProgmemRGBPalette16& pal,
                                 uint32_t now, float dt) {
  paletteOffset_ = fmodf(paletteOffset_ + dt * (2.0f + s.speed * 0.6f), 256.0f);
  for (uint16_t i = 0; i < n_; i++) {
    const uint8_t index   = (uint8_t)((int)paletteOffset_ + i * 160 / n_);
    const uint8_t shimmer = inoise8(i * 60, (uint16_t)(now / 6));
    leds_[idx(s, i)] = ColorFromPalette(pal, index, 150 + scale8(shimmer, 105), LINEARBLEND);
  }
}

// Peak flash (readme.md Effect 2): the VU meter, plus a mostly white flash,
// slightly tinted by the note color, on each attack. Strength =
// max(0.35, attack strength), fading out over ~120 ms.
void EffectEngine::renderPeakFlash(const Settings& s, const AudioFeatures& a, uint32_t now) {
  static constexpr uint32_t FLASH_MS = 120;
  drawVu(s, a.level);
  if (a.attackDetected) {
    flashPeak_  = fmaxf(0.35f, a.attackStrength);
    flashStart_ = now;
    flashColor_ = a.pitchValid ? blend(CRGB(CRGB::White), noteColor(s, a.midiNote), 64) : CRGB(CRGB::White);
  }
  const uint32_t age = now - flashStart_;
  if (flashPeak_ > 0.0f && age < FLASH_MS) {
    const uint8_t k = (uint8_t)(255.0f * flashPeak_ * (1.0f - age / (float)FLASH_MS));
    for (uint16_t i = 0; i < n_; i++) nblend(leds_[i], flashColor_, k);
  }
}

// Sustain (readme.md Effect 7): the whole strip in the note color. Full
// brightness on a pick, a steady level-following brightness while the
// note rings, then a fade out. Trail = fade time (~0.1-2 s). The last note
// color is kept through the fade, so the strip does not jump to the static
// color the moment the note stops.
void EffectEngine::renderSustain(const Settings& s, const AudioFeatures& a, float dt) {
  if (a.pitchValid)                               sustainColor_ = noteColor(s, a.midiNote);
  else if (a.attackDetected || sustain_ <= 0.01f) sustainColor_ = toCRGB(s.staticColor);

  if (a.attackDetected)    sustain_ = 1.0f;
  else if (a.level > 0.0f) sustain_ += (a.level - sustain_) * fminf(1.0f, dt / 0.15f);
  else                     sustain_ = fmaxf(0.0f, sustain_ - dt / (0.1f + s.trail * 0.019f));

  fill_solid(leds_, n_, scaled(sustainColor_, sustain_));
}

// Center VU: the level bar grows from the middle out to both ends with a
// soft edge, green at the centre to red at the ends.
void EffectEngine::renderCenterVu(const AudioFeatures& a) {
  const float center = (n_ - 1) / 2.0f;
  const float reach  = constrain(a.level, 0.0f, 1.0f) * (center + 1.0f);   // LEDs from the centre
  for (uint16_t i = 0; i < n_; i++) {
    const float   d   = fabsf(i - center);
    const float   k   = constrain(reach - d, 0.0f, 1.0f);                    // soft edge
    const uint8_t hue = (uint8_t)(96.0f * (1.0f - d / fmaxf(center, 1.0f)));
    leds_[i] = scaled(CRGB(CHSV(hue, 255, 255)), k);
  }
}

// Fire: Fire2012-style flames rising from LED 0. Sparking grows with the
// level, each attack adds heat at the base, and the fire smoulders (embers
// on the bottom LEDs) when silent. Trail = flame height (less cooling).
// Cooling and sparking ranges were tuned for 16 LEDs by simulating the heat
// model: Fire2012's 30-LED defaults flood a short strip at high level.
void EffectEngine::renderFire(const Settings& s, const AudioFeatures& a) {
  const int     n        = min((int)n_, MAX_LEDS);
  const int     cooling  = map(s.trail, 1, 100, 160, 60);
  const uint8_t coolMax  = (uint8_t)min(255, cooling * 10 / max(1, n) + 2);
  const bool    playing  = a.level > 0.0f;
  const uint8_t sparking = playing ? (uint8_t)(50 + a.level * 110) : 25;

  for (int i = 0; i < n; i++) heat_[i] = qsub8(heat_[i], random8(0, coolMax));             // cool
  for (int k = n - 1; k >= 2; k--) heat_[k] = (heat_[k - 1] + heat_[k - 2] + heat_[k - 2]) / 3;   // rise
  if (random8() < sparking) {                                                               // ignite
    const uint8_t y = random8(3);
    heat_[y] = qadd8(heat_[y], playing ? random8(160, 255) : random8(60, 120));
  }
  if (!playing) {                                                                           // embers
    for (int y = 0; y < min(2, n); y++) heat_[y] = max(heat_[y], random8(40, 90));
  }
  if (a.attackDetected) {                                                                   // pick: heat burst
    const uint8_t boost = (uint8_t)(120 + 135 * fmaxf(0.3f, a.attackStrength));
    heat_[0] = qadd8(heat_[0], boost);
    if (n > 1) heat_[1] = qadd8(heat_[1], boost);
  }
  for (int i = 0; i < n; i++) leds_[idx(s, i)] = HeatColor(heat_[i]);
}

// Sparkle: each attack lights 2-10 random LEDs (more for harder picks) in
// the note color blended toward white; a ringing note keeps adding the
// odd dimmer spark. Trail = fade.
void EffectEngine::renderSparkle(const Settings& s, const AudioFeatures& a) {
  fadeToBlackBy(leds_, n_, trailFade(s));
  const CRGB base = noteOrStatic(s, a);
  if (a.attackDetected) {
    const int count = 2 + (int)lroundf(8.0f * constrain(a.attackStrength, 0.0f, 1.0f));
    for (int k = 0; k < count; k++) leds_[random16(n_)] = blend(base, CRGB(CRGB::White), random8(64, 160));
  }
  if (a.level > 0.0f && random8() < (uint8_t)(a.level * 40)) {   // up to ~9 sparks/s
    leds_[random16(n_)] |= scaled(blend(base, CRGB(CRGB::White), random8(32, 96)), 0.35f + 0.25f * a.level);
  }
}

// Comet: each attack launches a comet from LED 0 in the note color, up to
// four at once. Speed setting x pick strength = comet speed. Trail = tail.
void EffectEngine::renderComet(const Settings& s, const AudioFeatures& a, float dt) {
  fadeToBlackBy(leds_, n_, trailFade(s));
  if (a.attackDetected) {
    int slot = 0;   // free slot, otherwise the comet furthest along
    for (int c = 0; c < MAX_COMETS; c++) {
      if (!comets_[c].active) { slot = c; break; }
      if (comets_[c].pos > comets_[slot].pos) slot = c;
    }
    const float speed = (5.0f + s.speed * 0.4f) * fmaxf(0.3f, a.attackStrength);   // LEDs/s
    comets_[slot] = {0.0f, speed, noteOrStatic(s, a), true};
  }
  for (Comet& c : comets_) {
    if (!c.active) continue;
    c.pos += c.speed * dt;
    if (c.pos > n_ - 1) { c.active = false; continue; }
    dotAt(s, c.pos, c.color);
  }
}

// Step chase (readme.md "rhythm response"): each attack moves a 4-LED block
// one block along the strip with a quick ~60 ms glide, wrapping around.
// Brightness follows the level. Trail = fade behind it.
void EffectEngine::renderStepChase(const Settings& s, const AudioFeatures& a, uint32_t now) {
  static constexpr int      BLOCK    = 4;
  static constexpr uint32_t GLIDE_MS = 60;

  const float t   = (now - stepStart_) / (float)GLIDE_MS;
  const float e   = t >= 1.0f ? 1.0f : t * (2.0f - t);   // ease out
  const float pos = stepFrom_ + (stepTarget_ - stepFrom_) * e;
  if (a.attackDetected) {
    stepFrom_   = pos;
    stepTarget_ = stepTarget_ + BLOCK;
    if (stepTarget_ >= n_) { stepTarget_ -= n_; stepFrom_ -= n_; }   // wrap, glide continues across the end
    stepStart_  = now;
  }

  fadeToBlackBy(leds_, n_, trailFade(s));
  const CRGB  c    = scaled(noteOrStatic(s, a), a.level);
  const int   base = (int)floorf(pos);
  const float frac = pos - base;
  for (int k = 0; k <= BLOCK; k++) {   // block with soft leading/trailing edges
    const float w = k == 0 ? 1.0f - frac : (k == BLOCK ? frac : 1.0f);
    const int   i = ((base + k) % n_ + n_) % n_;
    leds_[idx(s, i)] |= scaled(c, w);
  }
}
