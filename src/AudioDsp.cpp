#include "AudioDsp.h"
#include <math.h>
#include "EnvelopeDetector.h"
#include "AttackDetector.h"
#include "GuitarSynth.h"

namespace {

constexpr float ATTACK_MS = 2.0f;   // envelope attack: follows a pick within a few ms

EnvelopeDetector env;
AttackDetector   att;
float            fs = 16000.0f, fullScale = 900.0f;
uint16_t         releaseMs = 0;

float  hop[AttackDetector::HOP];
size_t hopFill = 0;

float    db = -120.0f, level = 0.0f, rise = 0.0f, strength = 0.0f;
uint32_t attacks = 0;
float    prevMidi = 0.0f, velocity = 0.0f;
bool     havePrev = false;

portMUX_TYPE       mux = portMUX_INITIALIZER_UNLOCKED;
AudioDsp::Snapshot published = {};

}  // namespace

void AudioDsp::begin(float sampleRate, float fullScaleLsb) {
  fs        = sampleRate;
  fullScale = fullScaleLsb;
  att.configure(fs);
}

void AudioDsp::process(const float* ac, size_t n, const Settings& s, bool simulated) {
  if (s.releaseMs != releaseMs) {
    releaseMs = s.releaseMs;
    env.configure(fs, ATTACK_MS, releaseMs);
  }

  for (size_t i = 0; i < n; i++) {
    hop[hopFill++] = ac[i];
    if (hopFill < AttackDetector::HOP) continue;
    hopFill = 0;

    env.process(hop, AttackDetector::HOP);
    db    = EnvelopeDetector::toDb(env.envelope(), fullScale);
    level = EnvelopeDetector::normalize(db, s.gateDb, s.fullDb);
    if (att.processHop(hop, db, s.attackDb, db > s.gateDb)) {
      attacks++;
      strength = level;
    }
    rise = att.riseDb();
  }

  // Pitch: simulated guitar only, until Phase 4.
  bool  pitchValid = false;
  float midi = prevMidi;
  if (simulated) {
    const GuitarSynth::Truth t = GuitarSynth::truth();
    pitchValid = t.sounding && level > 0.0f;
    midi = t.midi;
  }
  const float dt = n / fs;
  if (havePrev && dt > 0.0f) {
    const float raw = (midi - prevMidi) / dt;
    velocity += (raw - velocity) * fminf(1.0f, dt * 10.0f);
  }
  prevMidi = midi;
  havePrev = true;

  Snapshot out = {level, db, rise, attacks, strength, pitchValid, midi, velocity};
  portENTER_CRITICAL(&mux);
  published = out;
  portEXIT_CRITICAL(&mux);
}

AudioDsp::Snapshot AudioDsp::snapshot() {
  portENTER_CRITICAL(&mux);
  Snapshot s = published;
  portEXIT_CRITICAL(&mux);
  return s;
}
