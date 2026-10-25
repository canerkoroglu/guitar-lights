#include "ButtonGesture.h"

ButtonGesture::Event ButtonGesture::update(bool pressed, uint32_t now) {
  if (!started_) {
    started_  = true;
    raw_      = pressed;
    rawSince_ = now;
    down_     = pressed;
    armed_    = !pressed;   // down at power-up: wait for a release first
    return NONE;
  }

  if (pressed != raw_) {
    raw_      = pressed;
    rawSince_ = now;
  }

  // Debounce: accept the raw state once it has been stable long enough.
  if (raw_ != down_ && now - rawSince_ >= debounceMs_) {
    down_ = raw_;
    if (down_) {
      downAt_    = now;
      holdFired_ = false;
    } else {
      const bool wasArmed = armed_;
      armed_ = true;
      if (!wasArmed || holdFired_) return NONE;
      return (now - downAt_ >= longMs_) ? LONG_PRESS : SHORT_PRESS;
    }
  }

  if (down_ && armed_ && !holdFired_ && now - downAt_ >= holdMs_) {
    holdFired_ = true;
    return HOLD;
  }
  return NONE;
}
