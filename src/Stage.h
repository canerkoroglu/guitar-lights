/*
 * Stage.h — what the player controls live, without the phone:
 * Phase 8 mode button, tuner, battery gauge.
 *
 * Button on GPIO25 to GND (internal pull-up, no other parts needed):
 *   short press  next preset (lights off: turn them on; tuner: leave tuner)
 *   long press   tuner on / off                        (0.6 - 2 s)
 *   hold 2 s     show the battery gauge for 2.5 s
 *
 * Tuner and gauge are live states, not saved settings: after a restart
 * the strip always comes back in its normal effect.
 *
 * Loop task only.
 */
#pragma once
#include <Arduino.h>
#include "ButtonGesture.h"

namespace Stage {
  constexpr int      BUTTON_GPIO = 25;
  constexpr uint32_t GAUGE_MS    = 2500;

  void begin();
  void loop(uint32_t now);                     // reads the button, runs its gestures

  void handle(ButtonGesture::Event e, uint32_t now);   // also used by the serial "press" command

  bool     tuner();
  void     setTuner(bool on);
  void     showBatteryGauge(uint32_t now);
  bool     gaugeActive(uint32_t now);
  uint32_t gaugeStartedAt();
}
