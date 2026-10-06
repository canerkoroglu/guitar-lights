#include "PluckSynth.h"
#include <cmath>

void PluckSynth::configure(float fs) {
  fs_ = fs;
  for (int k = 0; k < HARMONICS; k++) {
    const float ph = 2.0f * (float)M_PI * (0.37f * (k + 1));   // fixed, spread-out start phases
    c_[k] = std::cos(ph);
    s_[k] = std::sin(ph);
    amp_[k] = 0.0f;
    decay_[k] = 1.0f;
  }
  setPitch(f0_);
}

void PluckSynth::setTone(float fundamental, float inharmonicity) {
  fundamental_ = fundamental;
  inharm_      = inharmonicity;
}

float PluckSynth::partialHz(int k) const {
  const float n = (float)(k + 1);
  return n * f0_ * std::sqrt(1.0f + inharm_ * n * n);
}

void PluckSynth::setPitch(float hz) {
  f0_ = hz;
  for (int k = 0; k < HARMONICS; k++) {
    const float w = 2.0f * (float)M_PI * partialHz(k) / fs_;
    rc_[k] = std::cos(w);
    rs_[k] = std::sin(w);
    if (partialHz(k) > 0.45f * fs_) amp_[k] = 0.0f;   // keep harmonics below Nyquist
  }
}

void PluckSynth::pluck(float hz, float peakLsb) {
  setPitch(hz);

  // The pick catches the string and releases it from rest, so the old
  // vibration stops and the new one starts at zero velocity (what a
  // magnetic pickup sees). On a ringing string this cut is the click of
  // a re-pick.
  for (int k = 0; k < HARMONICS; k++) {
    c_[k] = 1.0f;
    s_[k] = 0.0f;
  }

  // Harmonic weights ~1/k^0.9, scaled so the waveform's actual peak
  // (all harmonics start in phase) equals peakLsb.
  float w[HARMONICS];
  for (int k = 0; k < HARMONICS; k++) {
    w[k] = (partialHz(k) < 0.45f * fs_) ? std::pow((float)(k + 1), -0.9f) : 0.0f;
  }
  w[0] *= fundamental_;
  float shapePeak = 0.0f;
  for (int j = 0; j < 256; j++) {
    const float th = 2.0f * (float)M_PI * j / 256.0f;
    float v = 0.0f;
    for (int k = 0; k < HARMONICS; k++) v += w[k] * std::sin((k + 1) * th);
    if (std::fabs(v) > shapePeak) shapePeak = std::fabs(v);
  }

  // Low notes ring longer; each higher harmonic decays faster.
  const float tau1 = 2.0f * std::pow(82.41f / hz, 0.35f);
  for (int k = 0; k < HARMONICS; k++) {
    const float tau = tau1 / (1.0f + 0.7f * k);
    amp_[k]   = shapePeak > 0.0f ? peakLsb * w[k] / shapePeak : 0.0f;
    decay_[k] = std::exp(-1.0f / (tau * fs_));
  }

  noiseAmp_   = 0.35f * peakLsb;                 // pick noise
  noiseDecay_ = std::exp(-1.0f / (0.003f * fs_));  // ~3 ms
}

void PluckSynth::glide(float hz) { setPitch(hz); }

void PluckSynth::mute() {
  const float d = std::exp(-1.0f / (0.02f * fs_));
  for (int k = 0; k < HARMONICS; k++) decay_[k] = d;
}

void PluckSynth::render(float* out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    float v = 0.0f;
    for (int k = 0; k < HARMONICS; k++) {
      const float c = c_[k] * rc_[k] - s_[k] * rs_[k];
      const float s = c_[k] * rs_[k] + s_[k] * rc_[k];
      c_[k] = c;
      s_[k] = s;
      v += amp_[k] * s;
      amp_[k] *= decay_[k];
    }
    rng_ = rng_ * 1664525u + 1013904223u;
    v += noiseAmp_ * ((int32_t)rng_ / 2147483648.0f);
    noiseAmp_ *= noiseDecay_;
    out[i] = v;
  }
  // Keep phasors on the unit circle (float rounding drifts slowly).
  for (int k = 0; k < HARMONICS; k++) {
    const float r = 1.0f / std::sqrt(c_[k] * c_[k] + s_[k] * s_[k]);
    c_[k] *= r;
    s_[k] *= r;
  }
}

float PluckSynth::amplitude() const {
  float a = 0.0f;
  for (int k = 0; k < HARMONICS; k++) a += amp_[k];
  return a;
}
