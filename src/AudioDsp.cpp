#include "AudioDsp.h"
#include "AudioAnalyzer.h"
#include "GuitarSynth.h"

namespace {

AudioAnalyzer      analyzer;
float              fs  = 16000.0f;
float              cpu = 0.0f;
bool               sourceKnown = false, lastSimulated = false;
uint32_t           attackBase = 0;
portMUX_TYPE       mux = portMUX_INITIALIZER_UNLOCKED;
AudioDsp::Snapshot published = {};

}  // namespace

void AudioDsp::begin(float sampleRate, float fullScaleLsb) {
  fs = sampleRate;
  cpu = 0.0f;
  sourceKnown = false;
  attackBase = 0;
  analyzer.configure(sampleRate, fullScaleLsb);
}

void AudioDsp::process(const float* ac, size_t n, const Settings& s, bool simulated) {
  const uint32_t t0 = micros();

  if (!sourceKnown || simulated != lastSimulated) {
    // Do not let a note, predictor history, or envelope from the other
    // source leak into the newly selected source.  Keep attacks monotonic
    // because the UI consumes them as a sequence number.
    if (sourceKnown) attackBase += analyzer.output().attacks;
    analyzer.reset();
    sourceKnown = true;
    lastSimulated = simulated;
  }

  AnalyzerParams p;
  p.gateDb    = s.gateDb;
  p.fullDb    = s.fullDb;
  p.releaseMs = s.releaseMs;
  p.attackDb  = s.attackDb;
  p.pitchConf = s.pitchConf / 100.0f;
  analyzer.process(ac, n, p);

  // Share of real time spent here (n samples arrive every n/fs seconds).
  const float us = (float)(micros() - t0);
  cpu += (100.0f * us / (n * 1e6f / fs) - cpu) * 0.05f;

  float truth = -1.0f;
  if (simulated) {
    const GuitarSynth::Truth t = GuitarSynth::truth();
    if (t.sounding) truth = t.midi;
  }

  const AnalyzerOutput& o = analyzer.output();
  const Snapshot out = {o.level, o.db, o.rise, attackBase + o.attacks, o.attackStrength,
                        o.pitch.valid, o.pitch.midi, o.pitch.midiNote, o.pitch.cents,
                        o.pitch.confidence, o.velocity, truth, cpu};
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
