/*
 * AttackDetector — Phase 3 (readme.md): one event per pick attack.
 *
 * A guitar note is a stack of harmonics, and a re-pick of a ringing string
 * often barely changes the overall level, so neither "level went up" nor
 * "high frequencies went up" is reliable on its own (high notes keep their
 * own harmonics in the high band). What every attack does have is a part
 * that cannot be predicted from the vibration just before it: the pick
 * click, scrape noise, or a new pitch.
 *
 * So the detector runs an adaptive linear predictor (NLMS, 24 taps) over
 * the signal. It learns whatever harmonic mix the current note has and
 * cancels it; the prediction residual stays small on a ringing note and
 * jumps on an attack. Two smooth envelopes are checked every 4 ms hop:
 *
 *   residual   (unpredictable part, fast envelope) - re-picks, any register
 *   broadband  (the VU envelope, passed in)        - attacks from silence, palm mutes
 *
 * The onset value is how much either envelope rose over the last 12 ms:
 * large on the rising edge of an attack, back near zero once the level
 * settles, so the detector re-arms quickly for fast picking. Hysteresis
 * (re-arm below a third of the threshold) and a refractory period make one
 * pick give exactly one attack.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cstddef>
#include <cstdint>

class AttackDetector {
public:
  static constexpr size_t HOP = 64;   // samples per hop (4 ms at 16 kHz)

  void configure(float sampleRate, float refractoryMs = 60.0f);

  // One hop of AC samples. Returns true on a new attack.
  //   broadDb      broadband envelope in dB (EnvelopeDetector::toDb)
  //   thresholdDb  required rise over the last 12 ms (dB)
  //   gateOpen     overall level is above the noise gate
  bool processHop(const float* x, float broadDb, float thresholdDb, bool gateOpen);

  float riseDb() const { return rise_; }   // last onset value, for tuning

private:
  static constexpr int TAPS     = 24;     // predictor order: up to ~12 harmonics
  static constexpr int LOOKBACK = 3;      // hops (12 ms)

  float    w_[TAPS] = {}, h_[TAPS] = {};  // predictor weights, recent samples
  float    resEnv_ = 0.0f, resAtt_ = 1.0f, resRel_ = 1.0f;
  float    resHist_[LOOKBACK] = {}, broadHist_[LOOKBACK] = {};
  int      histPos_ = 0;
  bool     primed_  = false;
  float    rise_    = 0.0f;
  bool     armed_   = true;
  uint32_t hold_ = 0, refractoryHops_ = 15;
};
