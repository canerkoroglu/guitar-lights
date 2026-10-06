/*
 * PitchTracker — Phase 4 (readme.md): turns raw per-frame YIN estimates
 * into a steady note for the LEDs.
 *
 * - Small moves (up to 1 semitone per frame: vibrato, bends, slides) are
 *   followed with light smoothing.
 * - A bigger jump (a new note, or a one-frame octave glitch during a pick
 *   transient) must be seen in 2 consecutive frames before it is accepted.
 * - The reported note has hysteresis: it changes only when the pitch is
 *   more than 50 + hysteresis cents away from it, so a note that is a bit
 *   out of tune, or wobbles across a boundary, does not flicker.
 * - When estimates stop (decay, noise, pick transient) the last note is
 *   held for holdMs before the pitch becomes invalid.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cstdint>

struct PitchState {
  bool     valid       = false;
  float    midi        = 0.0f;   // continuous, smoothed MIDI pitch (45.0 = A2)
  int      midiNote    = -1;     // steady note with hysteresis, -1 when invalid
  float    cents       = 0.0f;   // current pitch relative to midiNote
  float    frequencyHz = 0.0f;
  float    confidence  = 0.0f;   // 1 - aperiodicity of the last accepted estimate
  uint32_t onsets      = 0;      // increments when a new note is accepted
};

class PitchTracker {
public:
  void configure(float frameRateHz, float holdMs = 150.0f, float hysteresisCents = 15.0f,
                 float smoothingMs = 25.0f);

  // One analysis frame: PitchDetector result plus whether the level is above the gate.
  const PitchState& update(bool found, float hz, float aperiodicity, bool gateOpen);
  const PitchState& state() const { return s_; }

private:
  PitchState s_;
  float alpha_ = 0.5f, hyst_ = 0.15f;
  int   holdFrames_ = 10, sinceGood_ = 0;
  float pending_ = 0.0f;
  int   pendingCount_ = 0;
};
