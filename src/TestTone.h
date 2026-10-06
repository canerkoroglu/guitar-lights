/*
 * TestTone.h — a known signal for checking the ADC path without a guitar.
 *
 * Uses the ESP32 DAC's built-in cosine generator on GPIO26. With a jumper
 * from GPIO26 to GPIO34, and nothing else connected to GPIO34, the Guitar
 * input scope should show a clean sine. Remove the jumper before wiring
 * the buffer circuit to GPIO34: two outputs must never drive one pin.
 */
#pragma once

namespace TestTone {
  constexpr int GPIO = 26;   // DAC channel 2 (GPIO25 is reserved for the mode button)
  void set(bool on);
  bool active();
}
