/*
 * EnvelopeDetector — Phase 2 (readme.md): amplitude envelope of the
 * DC-free guitar signal, with separate attack and release times, plus
 * the dB mapping used for the VU level.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cstddef>

class EnvelopeDetector {
public:
  void  configure(float sampleRate, float attackMs, float releaseMs);
  void  process(const float* x, size_t n);   // AC samples, ADC LSB units
  float envelope() const { return env_; }     // peak envelope, LSB
  float rms() const { return rms_; }          // RMS of the last process() call, LSB

  // Envelope in dB relative to fullScaleLsb (the clean swing above bias).
  static float toDb(float envLsb, float fullScaleLsb);
  // 0..1 level: gateDb -> 0, fullDb -> 1, linear in dB between.
  static float normalize(float db, float gateDb, float fullDb);

private:
  float aAtt_ = 1.0f, aRel_ = 1.0f;   // one-pole smoothing coefficients
  float env_  = 0.0f, rms_ = 0.0f;
};
