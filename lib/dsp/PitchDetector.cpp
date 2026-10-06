#include "PitchDetector.h"
#include <cmath>

void PitchDetector::configure(float fs, float minHz, float maxHz, size_t window) {
  fs_     = fs;
  window_ = window;
  tauMin_ = (size_t)std::floor(fs / maxHz);
  tauMax_ = (size_t)std::ceil(fs / minHz);
  if (tauMin_ < 2) tauMin_ = 2;
  d_.assign(tauMax_ + 2, 0.0f);
}

PitchDetector::Result PitchDetector::detect(const float* x, float threshold) {
  const size_t last = tauMax_ + 1;   // one extra lag for interpolation at tauMax

  // 1. Difference function. Four independent sums: with one accumulator
  // every multiply-add waits for the previous one (latency-bound on the
  // ESP32 FPU); four let them overlap. This is the hot loop (~88k
  // multiply-adds per frame).
  for (size_t tau = 1; tau <= last; tau++) {
    const float* b = x + tau;
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    size_t j = 0;
    for (; j + 4 <= window_; j += 4) {
      const float d0 = x[j] - b[j], d1 = x[j + 1] - b[j + 1];
      const float d2 = x[j + 2] - b[j + 2], d3 = x[j + 3] - b[j + 3];
      s0 += d0 * d0;
      s1 += d1 * d1;
      s2 += d2 * d2;
      s3 += d3 * d3;
    }
    for (; j < window_; j++) {
      const float df = x[j] - b[j];
      s0 += df * df;
    }
    d_[tau] = (s0 + s1) + (s2 + s3);
  }

  // 2. Cumulative mean normalized difference, in place.
  d_[0] = 1.0f;
  float run = 0.0f;
  for (size_t tau = 1; tau <= last; tau++) {
    run += d_[tau];
    d_[tau] = run > 0.0f ? d_[tau] * (float)tau / run : 1.0f;
  }

  // 3. First dip below the threshold, then down to its local minimum.
  size_t best = 0;
  for (size_t tau = tauMin_; tau <= tauMax_; tau++) {
    if (d_[tau] < threshold) {
      while (tau < tauMax_ && d_[tau + 1] < d_[tau]) tau++;
      best = tau;
      break;
    }
  }
  if (!best) return {false, 0.0f, 1.0f};

  // 4. Parabolic interpolation around the minimum.
  const float s0 = d_[best - 1], s1 = d_[best], s2 = d_[best + 1];
  const float den = s0 - 2.0f * s1 + s2;
  float tau = (float)best;
  if (den > 0.0f) tau += 0.5f * (s0 - s2) / den;
  return {true, fs_ / tau, s1};
}
