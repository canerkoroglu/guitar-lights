/*
 * ButtonGesture — Phase 8 (readme.md): turns the raw state of one
 * momentary button into gestures. Plain C++ (no Arduino), so it also
 * runs in host unit tests.
 *
 *   short press   released before longMs              -> SHORT_PRESS (on release)
 *   long press    released between longMs and holdMs  -> LONG_PRESS  (on release)
 *   hold          still held at holdMs                -> HOLD (while held, so it
 *                                                        gives feedback; nothing
 *                                                        more on release)
 *
 * Contact bounce is filtered: a new raw state must stay unchanged for
 * debounceMs before it counts. A button that is already down at power-up
 * (stuck, or held while switching on) is ignored until it is released.
 */
#pragma once
#include <cstdint>

class ButtonGesture {
public:
  enum Event : uint8_t { NONE = 0, SHORT_PRESS, LONG_PRESS, HOLD };

  explicit ButtonGesture(uint32_t debounceMs = 25, uint32_t longMs = 600, uint32_t holdMs = 2000)
      : debounceMs_(debounceMs), longMs_(longMs), holdMs_(holdMs) {}

  // Call often (every loop()). pressed = raw contact state, true = down.
  Event update(bool pressed, uint32_t nowMs);

  bool isDown() const { return down_; }   // debounced state

private:
  uint32_t debounceMs_, longMs_, holdMs_;
  bool     started_   = false;   // first update() seen
  bool     armed_     = false;   // a release has been seen since power-up
  bool     raw_       = false;   // last raw sample
  uint32_t rawSince_  = 0;       // when raw_ last changed
  bool     down_      = false;   // debounced state
  uint32_t downAt_    = 0;       // when the debounced press began
  bool     holdFired_ = false;
};
