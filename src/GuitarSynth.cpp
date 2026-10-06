#include "GuitarSynth.h"
#include <math.h>
#include "PluckSynth.h"

namespace {

constexpr float PEAK_V      = 0.5f;   // waveform peak of a full-strength pick
constexpr float NOISE_LSB   = 3.0f;   // ADC noise, +- LSB
constexpr float HUM_LSB     = 1.5f;   // 50 Hz pickup hum
constexpr size_t MAX_BLOCK  = 512;

PluckSynth synth;
float      fs = 16000.0f, bias = 2000.0f, lsbPerV = 1200.0f;
uint64_t   sampleClock = 0;           // samples rendered
double     nextEvent = 0.5;
float      noteMidi = 52.0f;          // current note (E3)
int16_t    lastManual = -2;
volatile bool pluckPending = false;

// Slide state
bool   gliding = false;
double glideStart = 0, glideDur = 0;
float  glideFrom = 52, glideTo = 52;

uint32_t rng = 0x9e3779b9u;
float    rnd() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }   // 0..1
float    hz(float midi) { return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f); }

void pluckAt(float midi, float strength) {
  noteMidi = midi;
  gliding  = false;
  synth.pluck(hz(midi), strength * PEAK_V * lsbPerV);
}

// Auto-play: a loose melodic line with varied dynamics, slides and rests.
void autoPlay(const Settings& s, double t) {
  const float lo = s.pitchLow, hi = s.pitchHigh;
  const float r  = rnd();
  if (r < 0.12f) {                                   // rest
    synth.mute();
    nextEvent = t + 0.8 + rnd() * 0.8;
    return;
  }
  float step = roundf((rnd() - 0.5f) * 10.0f);       // -5..+5 semitones
  if (step == 0) step = 2;
  const float target = constrain(noteMidi + step, lo, hi);
  if (r < 0.30f && synth.amplitude() > 20.0f) {      // slide, no new pick
    gliding    = true;
    glideStart = t;
    glideDur   = 0.12 + rnd() * 0.15;
    glideFrom  = noteMidi;
    glideTo    = target;
    nextEvent  = t + 0.3 + rnd() * 0.4;
    return;
  }
  pluckAt(target, 0.35f + rnd() * 0.65f);           // pick, soft to hard
  nextEvent = t + 0.18 + rnd() * 0.5;
}

}  // namespace

void GuitarSynth::begin(float sampleRate, float biasRaw, float lsbPerVolt) {
  fs      = sampleRate;
  bias    = biasRaw;
  lsbPerV = lsbPerVolt;
  synth.configure(fs);
}

void GuitarSynth::pluck() { pluckPending = true; }

void GuitarSynth::render(uint16_t* raw, size_t n, const Settings& s) {
  const double t = (double)sampleClock / fs;

  if (s.testAuto) {
    if (t >= nextEvent) autoPlay(s, t);
  } else if (s.testNote != lastManual) {
    lastManual = s.testNote;
    if (s.testNote >= 0) pluckAt(s.testNote, s.testLevel);
  }
  if (pluckPending) {
    pluckPending = false;
    const float note = (!s.testAuto && s.testNote >= 0) ? s.testNote : noteMidi;
    pluckAt(note, s.testAuto ? 0.8f : s.testLevel);
  }

  if (gliding) {
    const float frac = (float)((t - glideStart) / glideDur);
    noteMidi = frac >= 1.0f ? glideTo : glideFrom + (glideTo - glideFrom) * frac;
    if (frac >= 1.0f) gliding = false;
    synth.glide(hz(noteMidi));
  }

  static float v[MAX_BLOCK];
  if (n > MAX_BLOCK) n = MAX_BLOCK;
  synth.render(v, n);
  for (size_t i = 0; i < n; i++) {
    const double ts = (double)(sampleClock + i) / fs;
    const float x = bias + v[i]
                  + NOISE_LSB * (rnd() + rnd() - 1.0f)
                  + HUM_LSB * (float)sin(2.0 * M_PI * 50.0 * ts);
    raw[i] = (uint16_t)constrain(lroundf(x), 0L, 4095L);
  }
  sampleClock += n;
}

GuitarSynth::Truth GuitarSynth::truth() {
  return {synth.amplitude() > 2.0f, noteMidi};
}
