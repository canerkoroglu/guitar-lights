/*
 * AudioAnalyzer — the whole analysis chain for DC-free guitar samples:
 *
 *   EnvelopeDetector -> dB -> 0..1 level           Phase 2
 *   AttackDetector   -> attack events              Phase 3
 *   PitchDetector    -> YIN estimate every 16 ms   Phase 4
 *   PitchTracker     -> steady note, cents, hold   Phase 4
 *
 * Plain C++: the firmware (src/AudioDsp) and the host unit tests run
 * exactly the same code.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "EnvelopeDetector.h"
#include "AttackDetector.h"
#include "PitchDetector.h"
#include "PitchTracker.h"

struct AnalyzerParams {
  float gateDb    = -45.0f;   // quieter = silence (level 0, no attacks, no pitch)
  float fullDb    = -6.0f;    // this loud = level 1
  float releaseMs = 120.0f;   // level fall time
  float attackDb  = 6.0f;     // attack threshold: rise within 12 ms
  float pitchConf = 0.85f;    // minimum periodicity for a pitch (1 - YIN threshold)
};

struct AnalyzerOutput {
  float      level = 0.0f, db = -120.0f, rise = 0.0f;
  uint32_t   attacks = 0;          // increments once per attack
  float      attackStrength = 0.0f;
  PitchState pitch;
  float      velocity = 0.0f;      // semitones per second of pitch.midi
};

class AudioAnalyzer {
public:
  static constexpr size_t PITCH_HOP    = 256;    // YIN every 16 ms
  static constexpr size_t PITCH_WINDOW = 384;    // YIN integration window (24 ms)
  static constexpr float  MIN_HZ       = 70.0f;  // below low E, covers drop D (73.4 Hz)
  static constexpr float  MAX_HZ       = 1400.0f;// above E6, 24th fret on high E

  void configure(float sampleRate, float fullScaleLsb);
  void process(const float* ac, size_t n, const AnalyzerParams& p);
  const AnalyzerOutput& output() const { return out_; }

private:
  void analyzePitch(const AnalyzerParams& p);

  float            fs_ = 16000.0f, fullScale_ = 900.0f, releaseMs_ = -1.0f;
  EnvelopeDetector env_;
  AttackDetector   att_;
  PitchDetector    yin_;
  PitchTracker     tracker_;

  float              hop_[AttackDetector::HOP] = {};
  size_t             hopFill_ = 0;
  std::vector<float> ring_, frame_;   // latest samples for YIN
  size_t             ringPos_ = 0, filled_ = 0, sincePitch_ = 0;
  float              prevMidi_ = 0.0f;
  bool               prevValid_ = false;
  AnalyzerOutput     out_;
};
