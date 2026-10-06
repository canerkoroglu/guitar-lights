/*
 * AudioFeatures.h — what the audio layer tells the effect engine each frame.
 *
 * Subset of the AudioFeatures model in readme.md, filled each frame from
 * AudioDsp. The same struct is produced whether the source is the real
 * guitar input or the simulated guitar, so effects never change.
 */
#pragma once

struct AudioFeatures {
  float level;           // 0..1 normalized amplitude (envelope)
  bool  attackDetected;  // true for exactly one frame per pick attack
  float attackStrength;  // 0..1
  bool  pitchValid;      // a single note is currently detected
  float midiNote;        // continuous MIDI pitch: 45.0 = A2, 45.5 = A2 + 50 cents
  int   note;            // steady note with hysteresis (use for colors), -1 if none
  float pitchVelocity;   // semitones per second, + rising / - falling
};
