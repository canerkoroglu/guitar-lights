/*
 * Host unit tests for the control logic (Phase 8 mode button, Phase 9
 * battery monitor, tuner scale).
 *
 *   pio test -e native
 */
#include <unity.h>
#include <cmath>
#include <vector>
#include "ButtonGesture.h"
#include "BatteryMonitor.h"
#include "TunerScale.h"

// ---- ButtonGesture ----

// Feeds a button script: {ms, pressed} steps, sampled every 1 ms like loop().
static std::vector<ButtonGesture::Event> play(ButtonGesture& b, const std::vector<std::pair<uint32_t, bool>>& script,
                                              uint32_t start = 1000) {
  std::vector<ButtonGesture::Event> out;
  uint32_t t = start;
  for (const auto& step : script) {
    for (uint32_t i = 0; i < step.first; i++, t++) {
      const ButtonGesture::Event e = b.update(step.second, t);
      if (e != ButtonGesture::NONE) out.push_back(e);
    }
  }
  return out;
}

void test_short_press() {
  ButtonGesture b;
  auto ev = play(b, {{100, false}, {150, true}, {100, false}});
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::SHORT_PRESS, ev[0]);
}

void test_long_press() {
  ButtonGesture b;
  auto ev = play(b, {{100, false}, {1000, true}, {100, false}});
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::LONG_PRESS, ev[0]);
}

void test_hold_fires_once_while_held_and_nothing_on_release() {
  ButtonGesture b;
  std::vector<ButtonGesture::Event> ev = play(b, {{100, false}, {2100, true}});
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::HOLD, ev[0]);
  ev = play(b, {{3000, true}, {100, false}}, 1000 + 2200);
  TEST_ASSERT_EQUAL(0, ev.size());
}

void test_contact_bounce_is_one_press() {
  ButtonGesture b;
  // Bounce on press and on release: 1-3 ms flickers.
  auto ev = play(b, {{100, false}, {2, true}, {1, false}, {3, true}, {2, false}, {200, true},
                     {1, false}, {2, true}, {3, false}, {200, false}});
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::SHORT_PRESS, ev[0]);
}

void test_glitch_shorter_than_debounce_is_ignored() {
  ButtonGesture b;
  auto ev = play(b, {{100, false}, {10, true}, {200, false}});
  TEST_ASSERT_EQUAL(0, ev.size());
}

void test_button_down_at_power_up_is_ignored_until_released() {
  ButtonGesture b;
  auto ev = play(b, {{3000, true}, {100, false}});   // stuck/held at boot, no HOLD, no press
  TEST_ASSERT_EQUAL(0, ev.size());
  ev = play(b, {{150, true}, {100, false}}, 1000 + 3100);   // works normally afterwards
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::SHORT_PRESS, ev[0]);
}

void test_millis_wraparound() {
  ButtonGesture b;
  auto ev = play(b, {{100, false}, {1000, true}, {100, false}}, 0xFFFFFFFFu - 500u);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(ButtonGesture::LONG_PRESS, ev[0]);
}

// ---- BatteryMonitor ----

void test_battery_percent_curve() {
  TEST_ASSERT_EQUAL(100, BatteryMonitor::percentFor(4.25f));
  TEST_ASSERT_EQUAL(100, BatteryMonitor::percentFor(4.20f));
  TEST_ASSERT_EQUAL(50,  BatteryMonitor::percentFor(3.80f));
  TEST_ASSERT_EQUAL(0,   BatteryMonitor::percentFor(3.20f));
  int last = -1;   // monotonic
  for (float v = 3.2f; v <= 4.25f; v += 0.01f) {
    const int p = BatteryMonitor::percentFor(v);
    TEST_ASSERT_TRUE(p >= last);
    last = p;
  }
}

void test_battery_states_with_hysteresis() {
  BatteryMonitor m;
  TEST_ASSERT_EQUAL((int)BatteryState::Unknown, (int)m.state());
  TEST_ASSERT_EQUAL(-1, m.percent());

  m.addReading(3.95f);
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)m.state());

  for (int i = 0; i < 30; i++) m.addReading(3.48f);    // drains below Low
  TEST_ASSERT_EQUAL((int)BatteryState::Low, (int)m.state());

  for (int i = 0; i < 30; i++) m.addReading(3.55f);    // above Low, inside hysteresis: stays Low
  TEST_ASSERT_EQUAL((int)BatteryState::Low, (int)m.state());

  for (int i = 0; i < 30; i++) m.addReading(3.30f);
  TEST_ASSERT_EQUAL((int)BatteryState::Critical, (int)m.state());

  for (int i = 0; i < 30; i++) m.addReading(3.40f);    // inside hysteresis: stays Critical
  TEST_ASSERT_EQUAL((int)BatteryState::Critical, (int)m.state());

  for (int i = 0; i < 30; i++) m.addReading(4.10f);    // charged
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)m.state());
}

void test_battery_single_dip_is_smoothed() {
  BatteryMonitor m;
  for (int i = 0; i < 5; i++) m.addReading(3.70f);
  m.addReading(3.40f);   // one reading under a burst of LED load
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)m.state());
}

void test_battery_implausible_readings_ignored() {
  BatteryMonitor m;
  TEST_ASSERT_FALSE(m.addReading(0.3f));    // floating pin / no divider
  TEST_ASSERT_FALSE(m.addReading(6.0f));
  TEST_ASSERT_FALSE(m.addReading(NAN));
  TEST_ASSERT_EQUAL((int)BatteryState::Unknown, (int)m.state());
  TEST_ASSERT_TRUE(m.addReading(3.9f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.9f, m.volts());
}

// ---- Tuner scale ----

void test_tuner_scale() {
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.5f, tuner::position(0.0f, 16));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, tuner::position(-50.0f, 16));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 15.0f, tuner::position(50.0f, 16));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 15.0f, tuner::position(80.0f, 16));    // clamped
  TEST_ASSERT_TRUE(tuner::position(-10.0f, 16) < tuner::position(10.0f, 16));
  TEST_ASSERT_TRUE(tuner::zone(2.0f) == tuner::Zone::InTune);
  TEST_ASSERT_TRUE(tuner::zone(-3.0f) == tuner::Zone::InTune);
  TEST_ASSERT_TRUE(tuner::zone(-10.0f) == tuner::Zone::Close);
  TEST_ASSERT_TRUE(tuner::zone(20.0f) == tuner::Zone::Off);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_press);
  RUN_TEST(test_long_press);
  RUN_TEST(test_hold_fires_once_while_held_and_nothing_on_release);
  RUN_TEST(test_contact_bounce_is_one_press);
  RUN_TEST(test_glitch_shorter_than_debounce_is_ignored);
  RUN_TEST(test_button_down_at_power_up_is_ignored_until_released);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_battery_percent_curve);
  RUN_TEST(test_battery_states_with_hysteresis);
  RUN_TEST(test_battery_single_dip_is_smoothed);
  RUN_TEST(test_battery_implausible_readings_ignored);
  RUN_TEST(test_tuner_scale);
  return UNITY_END();
}
