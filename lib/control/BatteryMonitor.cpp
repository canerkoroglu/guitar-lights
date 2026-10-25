#include "BatteryMonitor.h"

constexpr float BatteryMonitor::LOW_V;
constexpr float BatteryMonitor::CRITICAL_V;
constexpr float BatteryMonitor::HYSTERESIS;
constexpr float BatteryMonitor::MIN_VALID_V;
constexpr float BatteryMonitor::MAX_VALID_V;

namespace {

constexpr float SMOOTHING = 0.25f;   // weight of each new reading

// Typical Li-ion open-circuit-ish curve at light load (volts, percent).
struct Point { float v; int pct; };
const Point CURVE[] = {
  {3.30f, 0}, {3.45f, 5}, {3.60f, 15}, {3.70f, 30}, {3.80f, 50},
  {3.90f, 62}, {4.00f, 75}, {4.10f, 88}, {4.20f, 100},
};
constexpr int CURVE_N = sizeof(CURVE) / sizeof(CURVE[0]);

}  // namespace

int BatteryMonitor::percentFor(float v) {
  if (v <= CURVE[0].v) return 0;
  if (v >= CURVE[CURVE_N - 1].v) return 100;
  for (int i = 1; i < CURVE_N; i++) {
    if (v < CURVE[i].v) {
      const Point& a = CURVE[i - 1];
      const Point& b = CURVE[i];
      const float  t = (v - a.v) / (b.v - a.v);
      return a.pct + (int)(t * (b.pct - a.pct) + 0.5f);
    }
  }
  return 100;
}

bool BatteryMonitor::addReading(float v) {
  if (!(v >= MIN_VALID_V && v <= MAX_VALID_V)) return false;   // also rejects NaN

  volts_ = have_ ? volts_ + (v - volts_) * SMOOTHING : v;
  have_  = true;

  // Thresholds with hysteresis: entering needs the threshold, leaving
  // needs threshold + HYSTERESIS.
  switch (state_) {
    case BatteryState::Unknown:
      state_ = volts_ < CRITICAL_V ? BatteryState::Critical
             : volts_ < LOW_V      ? BatteryState::Low
                                   : BatteryState::Ok;
      break;
    case BatteryState::Ok:
      if (volts_ < CRITICAL_V)  state_ = BatteryState::Critical;
      else if (volts_ < LOW_V)  state_ = BatteryState::Low;
      break;
    case BatteryState::Low:
      if (volts_ < CRITICAL_V)                state_ = BatteryState::Critical;
      else if (volts_ > LOW_V + HYSTERESIS)   state_ = BatteryState::Ok;
      break;
    case BatteryState::Critical:
      if (volts_ > LOW_V + HYSTERESIS)             state_ = BatteryState::Ok;
      else if (volts_ > CRITICAL_V + HYSTERESIS)   state_ = BatteryState::Low;
      break;
  }
  return true;
}

void BatteryMonitor::reset() {
  state_ = BatteryState::Unknown;
  volts_ = 0.0f;
  have_  = false;
}

int BatteryMonitor::percent() const { return have_ ? percentFor(volts_) : -1; }

const char* BatteryMonitor::name(BatteryState s) {
  switch (s) {
    case BatteryState::Ok:       return "ok";
    case BatteryState::Low:      return "low";
    case BatteryState::Critical: return "critical";
    default:                     return "unknown";
  }
}
