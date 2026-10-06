/*
 * AudioDsp.h — turns DC-free samples into features for the effects.
 *
 *   Phase 2  EnvelopeDetector -> dB -> 0..1 VU level (gate / full scale)
 *   Phase 3  AttackDetector   -> attack events with strength
 *   Phase 4  pitch: not yet. With the simulated guitar, the note being
 *            played is passed through so pitch effects keep working.
 *
 * process() runs in the audio task; snapshot() is safe from any task.
 */
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace AudioDsp {
  void begin(float sampleRate, float fullScaleLsb);
  void process(const float* ac, size_t n, const Settings& s, bool simulated);

  struct Snapshot {
    float    level;           // 0..1 VU level
    float    db;              // envelope, dB re full scale
    float    rise;            // last onset value (dB in 12 ms), for tuning
    uint32_t attacks;         // increments once per detected attack
    float    attackStrength;  // level at the last attack
    bool     pitchValid;
    float    midi;            // continuous MIDI pitch
    float    velocity;        // semitones per second
  };
  Snapshot snapshot();
}
