#include "PitchTracker.h"
#include <cmath>

static constexpr float JUMP    = 1.0f;    // semitones per frame still treated as the same note moving
static constexpr float AGREE   = 0.75f;   // two frames of a new note must agree this closely
static constexpr int   CONFIRM = 2;       // frames needed to accept a new note

void PitchTracker::configure(float frameRateHz, float holdMs, float hysteresisCents,
                             float smoothingMs) {
  const float frameMs = 1000.0f / frameRateHz;
  alpha_      = 1.0f - std::exp(-frameMs / smoothingMs);
  hyst_       = hysteresisCents / 100.0f;
  holdFrames_ = (int)std::ceil(holdMs / frameMs);
}

const PitchState& PitchTracker::update(bool found, float hz, float aperiodicity, bool gateOpen) {
  if (found && gateOpen && hz > 0.0f) {
    const float m = 69.0f + 12.0f * std::log2(hz / 440.0f);
    bool accepted = false;

    if (s_.valid && std::fabs(m - s_.midi) <= JUMP) {
      s_.midi += (m - s_.midi) * alpha_;       // same note moving: smooth
      pendingCount_ = 0;
      accepted = true;
    } else {
      if (pendingCount_ > 0 && std::fabs(m - pending_) <= AGREE) {
        pending_ = 0.5f * (pending_ + m);
        pendingCount_++;
      } else {
        pending_      = m;
        pendingCount_ = 1;
      }
      if (pendingCount_ >= CONFIRM) {          // new note confirmed: jump, no hysteresis
        s_.midi     = pending_;
        s_.midiNote = (int)std::lround(s_.midi);
        s_.valid    = true;
        s_.onsets++;
        pendingCount_ = 0;
        accepted = true;
      }
    }

    if (accepted) {
      sinceGood_ = 0;
      const int nearest = (int)std::lround(s_.midi);
      if (nearest != s_.midiNote && std::fabs(s_.midi - s_.midiNote) > 0.5f + hyst_) {
        s_.midiNote = nearest;
      }
      s_.cents       = (s_.midi - s_.midiNote) * 100.0f;
      s_.frequencyHz = 440.0f * std::pow(2.0f, (s_.midi - 69.0f) / 12.0f);
      s_.confidence  = 1.0f - aperiodicity;
      return s_;
    }
  } else {
    pendingCount_ = 0;
  }

  // Nothing accepted this frame: hold the last note, then drop it.
  if (s_.valid && ++sinceGood_ > holdFrames_) {
    s_.valid    = false;
    s_.midiNote = -1;
  }
  return s_;
}
