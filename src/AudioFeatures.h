/*
 * AudioFeatures.h — what the audio layer tells the effect engine each frame.
 *
 * Subset of the AudioFeatures model in readme.md. Produced by AudioSim for
 * now; the real ADC + DSP pipeline (Phase 1-4) will fill the same struct,
 * so effects do not change when real guitar input arrives.
 */
#pragma once

struct AudioFeatures {
  float level;           // 0..1 normalized amplitude (envelope)
  bool  attackDetected;  // true for exactly one frame per pick attack
  float attackStrength;  // 0..1
  bool  pitchValid;      // a single note is currently detected
  float midiNote;        // continuous MIDI pitch: 45.0 = A2, 45.5 = A2 + 50 cents
  float pitchVelocity;   // semitones per second, + rising / - falling
};
