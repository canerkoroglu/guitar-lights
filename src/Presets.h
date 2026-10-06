/*
 * Presets.h — one-tap looks.
 *
 * A preset sets only the effect, speed, trail, static color and note
 * colors. It never touches power, brightness, pitch range, Reverse, the
 * audio source and calibration (noise gate, full scale, release, attack
 * threshold) or the simulated-guitar settings.
 *
 * Applied through ConfigStore::fromJson with the same deferred save as the
 * web page, so the Phase 8 mode button can cycle them without a phone.
 */
#pragma once
#include <Arduino.h>

namespace Presets {
  size_t      count();
  const char* name(size_t i);
  bool        apply(size_t i);   // false if i is out of range
}
