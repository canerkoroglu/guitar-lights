#include "AudioAnalyzer.h"
#include <cmath>
#include <algorithm>

static constexpr float ENV_ATTACK_MS = 2.0f;   // level follows a pick within a few ms

void AudioAnalyzer::configure(float fs, float fullScaleLsb) {
  fs_        = fs;
  fullScale_ = fullScaleLsb;
  att_.configure(fs);
  yin_.configure(fs, MIN_HZ, MAX_HZ, PITCH_WINDOW);
  tracker_.configure(fs / PITCH_HOP);
  ring_.assign(yin_.frameSize(), 0.0f);
  frame_.assign(yin_.frameSize(), 0.0f);
  reset();
}

void AudioAnalyzer::reset() {
  env_ = EnvelopeDetector{};
  att_ = AttackDetector{};
  tracker_ = PitchTracker{};
  att_.configure(fs_);
  tracker_.configure(fs_ / PITCH_HOP);
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  std::fill(frame_.begin(), frame_.end(), 0.0f);
  hopFill_ = ringPos_ = filled_ = sincePitch_ = 0;
  prevMidi_ = 0.0f;
  prevValid_ = gateOpen_ = false;
  releaseMs_ = -1.0f;
  out_ = AnalyzerOutput{};
}

void AudioAnalyzer::process(const float* ac, size_t n, const AnalyzerParams& p) {
  if (p.releaseMs != releaseMs_) {
    releaseMs_ = p.releaseMs;
    env_.configure(fs_, ENV_ATTACK_MS, releaseMs_);
  }

  for (size_t i = 0; i < n; i++) {
    ring_[ringPos_] = ac[i];
    ringPos_ = (ringPos_ + 1) % ring_.size();
    if (filled_ < ring_.size()) filled_++;

    hop_[hopFill_++] = ac[i];
    if (hopFill_ == AttackDetector::HOP) {
      hopFill_ = 0;
      env_.process(hop_, AttackDetector::HOP);
      out_.db    = EnvelopeDetector::toDb(env_.envelope(), fullScale_);
      out_.level = EnvelopeDetector::normalize(out_.db, p.gateDb, p.fullDb);
      // Separate open/close thresholds prevent borderline ADC noise from
      // flickering the gate.  Attack and pitch consume this shared state.
      constexpr float GATE_HYSTERESIS_DB = 3.0f;
      if (gateOpen_) gateOpen_ = out_.db > p.gateDb - GATE_HYSTERESIS_DB;
      else           gateOpen_ = out_.db > p.gateDb;
      out_.gateOpen = gateOpen_;
      if (att_.processHop(hop_, out_.db, p.attackDb, gateOpen_)) {
        out_.attacks++;
        out_.attackStrength = out_.level;
      }
      out_.rise = att_.riseDb();
    }

    if (++sincePitch_ >= PITCH_HOP) {
      sincePitch_ = 0;
      analyzePitch(p);
    }
  }
}

void AudioAnalyzer::analyzePitch(const AnalyzerParams& p) {
  const bool gateOpen = gateOpen_;

  // YIN only when there is something to analyse: saves CPU in silence.
  PitchDetector::Result r = {false, 0.0f, 1.0f};
  if (gateOpen && filled_ == ring_.size()) {
    const size_t len = ring_.size();
    for (size_t i = 0; i < len; i++) frame_[i] = ring_[(ringPos_ + i) % len];   // oldest first
    r = yin_.detect(frame_.data(), 1.0f - p.pitchConf);
  }
  const PitchState& ps = tracker_.update(r.found, r.hz, r.aperiodicity, gateOpen);
  out_.pitch = ps;

  // Pitch velocity for rising/falling effects.
  const float dt = PITCH_HOP / fs_;
  if (ps.valid && prevValid_) {
    const float raw = (ps.midi - prevMidi_) / dt;
    out_.velocity += (raw - out_.velocity) * std::fmin(1.0f, dt * 10.0f);
  } else {
    out_.velocity *= 0.8f;
  }
  prevMidi_  = ps.midi;
  prevValid_ = ps.valid;
}
