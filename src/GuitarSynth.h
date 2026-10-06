/*
 * GuitarSynth.h — simulated guitar input for working without hardware.
 *
 * Produces raw ADC samples as GPIO34 would see them through the MCP6022
 * buffer: a plucked-string signal (lib/dsp/PluckSynth) on the 1.65 V
 * bias, plus ADC noise and a little mains hum. AudioInput substitutes
 * these for the real samples when Source = Simulated guitar, so the
 * whole DSP chain (DC removal, envelope, attacks) runs on them unchanged.
 *
 * Auto-play: picked notes, slides, rests and varied pick strength inside
 * the pitch range. Manual: the web UI's note, pick strength and Pluck.
 */
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace GuitarSynth {
  void begin(float sampleRate, float biasRaw, float lsbPerVolt);
  void render(uint16_t* raw, size_t n, const Settings& s);   // audio task only
  void pluck();                                               // any task

  struct Truth { bool sounding; float midi; };
  Truth truth();   // pitch being played (stands in for Phase 4 pitch detection)
}
