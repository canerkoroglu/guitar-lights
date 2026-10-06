/*
 * PluckSynth — plucked-string model for testing the audio pipeline
 * without a guitar.
 *
 * Sum of harmonics with per-harmonic exponential decay (high harmonics
 * die faster, as on a real string), plus a short pick-noise burst on each
 * attack. Pitch can glide without re-plucking (slides). Output is AC in
 * ADC LSB units; the caller adds bias and noise.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cstddef>
#include <cstdint>

class PluckSynth {
public:
  static constexpr int HARMONICS = 16;

  void  configure(float sampleRate);
  // Timbre for the next pluck. fundamental scales the 1st harmonic
  // (0 = missing fundamental). inharmonicity B stretches partial k to
  // k * f0 * sqrt(1 + B k^2), as on a stiff string (wound strings ~1e-4).
  void  setTone(float fundamental, float inharmonicity);
  void  pluck(float hz, float peakLsb);   // new attack at this pitch
  void  glide(float hz);                  // change pitch without re-plucking
  void  mute();                           // damp the string (~20 ms)
  void  render(float* out, size_t n);     // overwrites out[]

  float frequency() const { return f0_; }
  float amplitude() const;                // sum of harmonic amplitudes, LSB

private:
  void  setPitch(float hz);
  float partialHz(int k) const;   // frequency of harmonic k (0-based)

  float    fs_ = 16000.0f;
  float    f0_ = 82.41f;
  float    fundamental_ = 1.0f, inharm_ = 0.0f;
  float    c_[HARMONICS]  = {}, s_[HARMONICS]  = {};   // phasor per harmonic
  float    rc_[HARMONICS] = {}, rs_[HARMONICS] = {};   // rotation per sample
  float    amp_[HARMONICS] = {}, decay_[HARMONICS] = {};
  float    noiseAmp_ = 0.0f, noiseDecay_ = 0.0f;
  uint32_t rng_ = 0x12345678u;
};
