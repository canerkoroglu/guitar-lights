/*
 * PitchDetector — Phase 4 (readme.md): YIN estimate of the fundamental
 * frequency of a single note.
 *
 * Why YIN and not "strongest FFT peak": a guitar note is a stack of
 * harmonics, and on low strings the 2nd or 3rd harmonic is often louder
 * than the fundamental; with some pickups the fundamental is nearly
 * missing. YIN looks for the shortest lag at which the whole waveform
 * repeats. Each harmonic on its own repeats at shorter lags, but their sum
 * only repeats at the true period, so YIN reports the played note even
 * with a weak or missing fundamental.
 *
 *   1. difference function   d(t)  = sum_j (x[j] - x[j+t])^2
 *   2. cumulative mean norm  d'(t) = d(t) * t / sum_{1..t} d   (no false dips at tiny lags)
 *   3. first lag in range where d' dips below the threshold, walked down to its local minimum
 *   4. parabolic interpolation around that minimum for sub-sample precision
 *
 * d' at the chosen lag is the aperiodicity: 0 for a perfectly periodic
 * signal, ~1 for noise. 1 - aperiodicity is reported as confidence.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cstddef>
#include <vector>

class PitchDetector {
public:
  struct Result {
    bool  found;          // a dip below the threshold was found
    float hz;             // estimated fundamental
    float aperiodicity;   // d' at the chosen lag
  };

  void   configure(float sampleRate, float minHz, float maxHz, size_t window);
  size_t frameSize() const { return window_ + tauMax_ + 2; }   // samples per analysis

  // x: frameSize() samples, oldest first.
  // threshold: largest d' accepted as a pitch (0.15 = 85% confidence).
  Result detect(const float* x, float threshold);

private:
  float  fs_ = 16000.0f;
  size_t window_ = 0, tauMin_ = 2, tauMax_ = 2;
  std::vector<float> d_;
};
