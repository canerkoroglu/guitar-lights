#include "Battery.h"
#include "AudioInput.h"
#include <Preferences.h>

namespace {

constexpr uint32_t READ_EVERY_MS = 30000;   // the cell changes slowly
constexpr float    DIVIDER       = 2.0f;    // 100k / 100k

BatteryMonitor monitor;
uint32_t       lastSeq     = 0;
uint32_t       lastRequest = 0;
uint32_t       lastPinMv   = 0;
bool           wasFitted   = false;
int16_t        lastTrim    = 0;
bool           bootOnly    = false;

float cellVolts(uint32_t pinMv, int16_t trimMv) {
  return (pinMv * DIVIDER + trimMv) / 1000.0f;
}

}  // namespace

void Battery::begin() {
  Preferences p;
  p.begin("battery", false);   // read-write: creates the namespace, no "not found" log
  bootOnly = p.getBool("bootOnly", false);
  p.end();
}

void Battery::disableRuntimeReads() {
  Preferences p;
  p.begin("battery", false);
  p.putBool("bootOnly", true);
  p.end();
  bootOnly = true;
}

void Battery::loop(const Settings& s, uint32_t now) {
  if (!s.batteryFitted) {
    if (wasFitted) monitor.reset();
    wasFitted = false;
    return;
  }

  // Just switched on, or the trim changed: start over from the last reading
  // so the new calibration shows at once instead of after smoothing.
  if (!wasFitted || s.batteryTrimMv != lastTrim) {
    monitor.reset();
    if (lastPinMv) monitor.addReading(cellVolts(lastPinMv, s.batteryTrimMv));
    wasFitted = true;
    lastTrim  = s.batteryTrimMv;
  }

  const AudioInput::BatteryPin b = AudioInput::batteryPin();
  if (b.seq != lastSeq) {
    lastSeq   = b.seq;
    lastPinMv = b.mv;
    monitor.addReading(cellVolts(b.mv, s.batteryTrimMv));   // implausible readings are ignored
  }

  if (!bootOnly && now - lastRequest >= READ_EVERY_MS) {
    lastRequest = now;
    AudioInput::requestBatteryRead();
  }
}

Battery::Status Battery::status() {
  Status st;
  st.fitted  = wasFitted;
  st.state   = monitor.state();
  st.volts   = monitor.volts();
  st.percent = monitor.percent();
  st.pinMv   = lastPinMv;
  st.bootOnly = bootOnly;
  return st;
}
