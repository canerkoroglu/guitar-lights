/*
 * AudioDsp.h — firmware wrapper around lib/dsp AudioAnalyzer:
 *
 *   Phase 2  VU level        Phase 3  attacks
 *   Phase 4  pitch (YIN + tracker: steady note, cents, confidence)
 *
 * process() runs in the audio task; snapshot() is safe from any task.
 * With the simulated guitar it also reports the note actually being
 * played, so pitch detection can be checked against it.
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
    int      note;            // steady note with hysteresis, -1 if none
    float    cents;           // pitch relative to note
    float    confidence;      // 0..1 periodicity of the last accepted estimate
    float    velocity;        // semitones per second
    float    truthMidi;       // simulated guitar's note, -1 if not simulated or silent
    float    cpu;             // % of real time spent in process()
  };
  Snapshot snapshot();
}
