/*
 * BatteryMonitor — Phase 9 (readme.md): state of charge of the 1S Li-ion
 * pack (2 x 18650 in parallel) from occasional cell-voltage readings.
 * Plain C++ (no Arduino), so it also runs in host unit tests.
 *
 * - Readings are smoothed (the cell sags a little under LED load).
 * - Readings outside 2.5..4.5 V are treated as "no battery monitor
 *   fitted" (e.g. a floating ADC pin) and leave the state Unknown.
 * - Low and Critical have hysteresis, so a cell hovering at a threshold
 *   does not flip the warning on and off.
 *
 * This is information for the player only. Over-discharge protection is
 * the BMS's job, never this code's.
 */
#pragma once
#include <cstdint>

enum class BatteryState : uint8_t { Unknown = 0, Ok, Low, Critical };

class BatteryMonitor {
public:
  static constexpr float LOW_V       = 3.50f;   // enter Low below this
  static constexpr float CRITICAL_V  = 3.35f;   // enter Critical below this
  static constexpr float HYSTERESIS  = 0.10f;   // leave a state this far above its threshold
  static constexpr float MIN_VALID_V = 2.50f;
  static constexpr float MAX_VALID_V = 4.50f;

  // One cell-voltage reading. Returns false if it was implausible (ignored).
  bool addReading(float cellVolts);
  void reset();

  BatteryState state()   const { return state_; }
  float        volts()   const { return volts_; }       // smoothed, 0 if Unknown
  int          percent() const;                         // 0..100, -1 if Unknown

  // Rough state of charge of a Li-ion cell at light load, 0..100.
  static int percentFor(float cellVolts);

  static const char* name(BatteryState s);   // "unknown", "ok", "low", "critical"

private:
  BatteryState state_ = BatteryState::Unknown;
  float        volts_ = 0.0f;
  bool         have_  = false;
};
