/*
 * AudioInput.h — Phase 1 guitar input (readme.md).
 *
 * ADC1 on GPIO34 is sampled by the I2S peripheral straight into DMA
 * buffers, so sample timing is set by hardware, not by loop(), LED
 * updates, or Wi-Fi. A task on core 1 removes DC, computes statistics,
 * keeps the latest samples for a scope view, and feeds AudioDsp.
 *
 * With Source = Simulated guitar, GuitarSynth's samples replace the ADC
 * samples right after acquisition (the I2S clock still paces the task),
 * so everything downstream runs exactly as it will with the guitar.
 */
#pragma once
#include <Arduino.h>

namespace AudioInput {
  constexpr uint32_t SAMPLE_RATE = 16000;   // Hz, nominal
  constexpr float    ACTUAL_RATE = 15937.5f; // Hz: I2S 32000 * 255/256, halved (measured)
  constexpr int      ADC_GPIO    = 34;      // ADC1_CH6: input-only, usable with Wi-Fi on
  constexpr size_t   SCOPE_LEN   = 512;     // samples kept for the scope (32 ms)

  struct Stats {
    bool     running;
    float    sampleRate;    // measured, Hz
    float    dcV;           // mean input voltage, should sit near the 1.65 V bias
    float    minV, maxV;    // extremes over the last window
    float    p2pV;          // peak-to-peak
    float    rmsV;          // RMS after DC removal
    uint32_t clipped;       // samples outside the ADC's accurate range in the last window
    float    zeroCrossHz;   // upward zero crossings per second (test metric, not pitch)
    uint16_t rawLo, rawHi;  // raw limits of the accurate range (150..2450 mV)
  };

  bool   begin();
  Stats  stats();                           // latest 250 ms window, thread-safe copy
  size_t scope(uint16_t* out, size_t n);    // most recent raw samples, oldest first
}
