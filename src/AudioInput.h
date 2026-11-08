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
  constexpr int      BATTERY_GPIO = 35;     // ADC1_CH7: cell voltage through a 100k/100k divider

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
    bool     biasValid;     // DC bias is suitable for the analog guitar input
  };

  // Three-second quiet-input calibration.  It measures the AC RMS before
  // the DSP gate and proposes a gate 15 dB above that floor.
  struct Calibration {
    bool    active;
    bool    biasValid;
    uint8_t progress;          // 0..100
    float   noiseDb;           // RMS, dB re ADC full-scale swing
    int8_t  suggestedGateDb;
  };

  bool   begin();
  Stats  stats();                           // latest 250 ms window, thread-safe copy
  size_t scope(uint16_t* out, size_t n);    // most recent raw samples, oldest first
  bool   startNoiseCalibration();           // false when a run is already active
  Calibration calibration();                // thread-safe copy

  // Battery pin (Phase 9). ADC1 belongs to the I2S sampler, so the pin can
  // only be read by briefly handing ADC1 back, which drops ~80 ms of
  // audio. requestBatteryRead() therefore only asks: the audio task does
  // it at the next moment the input is below the noise gate, never while
  // you play. begin() takes the first reading before audio starts.
  struct BatteryPin {
    uint32_t seq;   // increments with every new reading; 0 = none yet
    uint32_t mv;    // calibrated voltage at GPIO35, millivolts
  };
  void       requestBatteryRead();          // any task
  BatteryPin batteryPin();                  // latest reading, thread-safe copy
  uint32_t   lastBatteryReadMs();           // millis() of the last runtime read, 0 = none

  // Blocks the audio task has completed. Stops advancing if the task stalls
  // (main.cpp restarts the board then: the LEDs must never freeze on stage).
  uint32_t   blockCount();
}
