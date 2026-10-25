/*
 * TunerScale — how the LED tuner shows how far a note is from pitch.
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 *
 * The middle of the strip is "in tune". A flat note shows to the low
 * side, a sharp note to the high side; the full half-strip is 50 cents.
 */
#pragma once

namespace tuner {

constexpr float IN_TUNE_CENTS = 3.0f;    // what a stage tuner calls "in tune"
constexpr float CLOSE_CENTS   = 15.0f;   // nearly there
constexpr float RANGE_CENTS   = 50.0f;   // edge of the strip

enum class Zone { InTune, Close, Off };

inline Zone zone(float cents) {
  const float c = cents < 0.0f ? -cents : cents;
  return c <= IN_TUNE_CENTS ? Zone::InTune : (c <= CLOSE_CENTS ? Zone::Close : Zone::Off);
}

// Fractional LED position (0 .. numLeds-1) for a cents offset. The
// centre, (numLeds-1)/2, is in tune; +-RANGE_CENTS reaches the ends.
inline float position(float cents, int numLeds) {
  const float half = (numLeds - 1) * 0.5f;
  float c = cents > RANGE_CENTS ? RANGE_CENTS : (cents < -RANGE_CENTS ? -RANGE_CENTS : cents);
  return half + c / RANGE_CENTS * half;
}

}  // namespace tuner
