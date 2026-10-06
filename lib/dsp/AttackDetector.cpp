#include "AttackDetector.h"
#include <cmath>

static constexpr float NLMS_MU        = 0.3f;   // adaptation step (0..2)
static constexpr float RES_ATTACK_MS  = 0.5f;
static constexpr float RES_RELEASE_MS = 20.0f;

static float onePole(float fs, float ms) {
  return 1.0f - std::exp(-1.0f / (fs * ms * 0.001f));
}

void AttackDetector::configure(float fs, float refractoryMs) {
  resAtt_ = onePole(fs, RES_ATTACK_MS);
  resRel_ = onePole(fs, RES_RELEASE_MS);

  const float hopMs = HOP * 1000.0f / fs;
  refractoryHops_ = (uint32_t)std::ceil(refractoryMs / hopMs);
}

bool AttackDetector::processHop(const float* x, float broadDb, float thresholdDb, bool gateOpen) {
  // Input power over the taps, recomputed per hop so it cannot drift.
  float norm = 0.0f;
  for (int i = 0; i < TAPS; i++) norm += h_[i] * h_[i];

  for (size_t n = 0; n < HOP; n++) {
    float pred = 0.0f;
    for (int i = 0; i < TAPS; i++) pred += w_[i] * h_[i];
    const float e = x[n] - pred;

    const float g = NLMS_MU * e / (norm + 1.0f);
    for (int i = 0; i < TAPS; i++) w_[i] += g * h_[i];

    norm += x[n] * x[n] - h_[TAPS - 1] * h_[TAPS - 1];
    if (norm < 0.0f) norm = 0.0f;
    for (int i = TAPS - 1; i > 0; i--) h_[i] = h_[i - 1];
    h_[0] = x[n];

    const float a = std::fabs(e);
    resEnv_ += (a - resEnv_) * (a > resEnv_ ? resAtt_ : resRel_);
  }
  const float resDb = 20.0f * std::log10(resEnv_ + 1e-3f);

  if (!primed_) {
    for (int i = 0; i < LOOKBACK; i++) { resHist_[i] = resDb; broadHist_[i] = broadDb; }
    primed_ = true;
  }
  // histPos_ holds the value from LOOKBACK hops ago.
  const float rRes = resDb - resHist_[histPos_];
  const float rAll = broadDb - broadHist_[histPos_];
  resHist_[histPos_]   = resDb;
  broadHist_[histPos_] = broadDb;
  histPos_ = (histPos_ + 1) % LOOKBACK;
  rise_ = rRes > rAll ? rRes : rAll;

  if (hold_) hold_--;
  if (armed_ && hold_ == 0 && gateOpen && rise_ > thresholdDb) {
    armed_ = false;
    hold_  = refractoryHops_;
    return true;
  }
  if (!armed_ && rise_ < thresholdDb / 3.0f) armed_ = true;
  return false;
}
