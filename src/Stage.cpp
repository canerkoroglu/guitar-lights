#include "Stage.h"
#include "Config.h"
#include "Presets.h"

namespace {

ButtonGesture button;
bool          tunerOn      = false;
bool          gaugeOn      = false;
uint32_t      gaugeStart   = 0;
size_t        nextPreset   = 0;   // the button walks through the presets in order

void stepPreset() {
  if (Presets::count() == 0) return;
  const size_t i = nextPreset;
  nextPreset = (nextPreset + 1) % Presets::count();
  if (Presets::apply(i)) Serial.printf("[button] preset %u %s\n", (unsigned)i, Presets::name(i));
}

}  // namespace

void Stage::begin() { pinMode(BUTTON_GPIO, INPUT_PULLUP); }

void Stage::loop(uint32_t now) {
  handle(button.update(digitalRead(BUTTON_GPIO) == LOW, now), now);
}

void Stage::handle(ButtonGesture::Event e, uint32_t now) {
  switch (e) {
    case ButtonGesture::SHORT_PRESS:
      if (tunerOn) {
        setTuner(false);
      } else if (!ConfigStore::get().power) {
        String err;
        ConfigStore::fromJson("{\"power\":true}", err);
        ConfigStore::requestSave();
        Serial.println(F("[button] lights on"));
      } else {
        stepPreset();
      }
      break;
    case ButtonGesture::LONG_PRESS:
      setTuner(!tunerOn);
      break;
    case ButtonGesture::HOLD:
      showBatteryGauge(now);
      break;
    default:
      break;
  }
}

bool Stage::tuner() { return tunerOn; }

void Stage::setTuner(bool on) {
  if (on == tunerOn) return;
  tunerOn = on;
  Serial.printf("[stage] tuner %s\n", on ? "on" : "off");
}

void Stage::showBatteryGauge(uint32_t now) {
  gaugeOn    = true;
  gaugeStart = now;
}

bool Stage::gaugeActive(uint32_t now) {
  if (gaugeOn && now - gaugeStart >= GAUGE_MS) gaugeOn = false;
  return gaugeOn;
}

uint32_t Stage::gaugeStartedAt() { return gaugeStart; }
