/*
 * DspMath.h — small helpers shared by the DSP code, the firmware and the
 * host tests, so each formula lives in one place.
 *
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 */
#pragma once
#include <cmath>

namespace dsp {

// Coefficient of a one-pole smoother y += (x - y) * a with time constant
// ms at sample rate fs. ms <= 0 means no smoothing (a = 1).
inline float onePoleCoeff(float fs, float ms) {
  return (ms <= 0.0f) ? 1.0f : 1.0f - std::exp(-1.0f / (fs * ms * 0.001f));
}

// Equal-tempered MIDI pitch <-> frequency, A4 = MIDI 69 = 440 Hz.
inline float midiToHz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }
inline float hzToMidi(float hz)   { return 69.0f + 12.0f * std::log2(hz / 440.0f); }

}  // namespace dsp
