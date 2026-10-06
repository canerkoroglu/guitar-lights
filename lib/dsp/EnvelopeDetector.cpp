#include "EnvelopeDetector.h"
#include <cmath>

static float onePole(float fs, float ms) {
  return (ms <= 0.0f) ? 1.0f : 1.0f - std::exp(-1.0f / (fs * ms * 0.001f));
}

void EnvelopeDetector::configure(float fs, float attackMs, float releaseMs) {
  aAtt_ = onePole(fs, attackMs);
  aRel_ = onePole(fs, releaseMs);
}

void EnvelopeDetector::process(const float* x, size_t n) {
  double sq = 0.0;
  for (size_t i = 0; i < n; i++) {
    const float a = std::fabs(x[i]);
    env_ += (a - env_) * (a > env_ ? aAtt_ : aRel_);
    sq   += (double)x[i] * x[i];
  }
  if (n) rms_ = (float)std::sqrt(sq / n);
}

float EnvelopeDetector::toDb(float envLsb, float fullScaleLsb) {
  return 20.0f * std::log10((envLsb + 1e-3f) / fullScaleLsb);
}

float EnvelopeDetector::normalize(float db, float gateDb, float fullDb) {
  if (fullDb <= gateDb) return db >= fullDb ? 1.0f : 0.0f;
  const float t = (db - gateDb) / (fullDb - gateDb);
  return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
