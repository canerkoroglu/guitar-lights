/*
 * Battery.h — Phase 9 battery monitor (readme.md "Battery monitor").
 *
 * Cell voltage = GPIO35 voltage x 2 (100k/100k divider) + trim. Readings
 * are requested every 30 s and taken by the audio task in a silent
 * moment (see AudioInput::requestBatteryRead), then smoothed and
 * classified by lib/control/BatteryMonitor.
 *
 * Loop task only. Does nothing unless Settings::batteryFitted is on.
 */
#pragma once
#include <Arduino.h>
#include "Config.h"
#include "BatteryMonitor.h"

namespace Battery {
  void begin();                                  // loads the "runtime reads disabled" flag
  void loop(const Settings& s, uint32_t now);   // call from loop()

  // Called by the audio watchdog just before a restart when the stall came
  // right after a runtime battery read: from then on the battery is only
  // read at boot (safe, before audio starts), never while running.
  void disableRuntimeReads();

  struct Status {
    bool         fitted;    // Settings::batteryFitted
    BatteryState state;     // Unknown until a plausible reading
    float        volts;     // smoothed cell voltage
    int          percent;   // 0..100, -1 if unknown
    uint32_t     pinMv;     // last raw pin reading, for calibration
    bool         bootOnly;  // runtime reads disabled after a driver stall
  };
  Status status();
}
