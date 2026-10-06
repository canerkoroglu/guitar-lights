/*
 * Host unit tests for the audio DSP (Phase 2 VU level, Phase 3 attacks).
 *
 * Plays scripted guitar parts through PluckSynth, adds ADC-like noise and
 * mains hum, and runs the same EnvelopeDetector / AttackDetector code the
 * firmware uses.
 *
 *   pio test -e native
 */
#include <unity.h>
#include <cmath>
#include <vector>
#include "EnvelopeDetector.h"
#include "AttackDetector.h"
#include "PluckSynth.h"

static constexpr float FS         = 15937.5f;   // actual rate on the ESP32
static constexpr float FULL_SCALE = 927.0f;     // LSB swing from 1.65 V bias to 2.45 V
static constexpr float GATE_DB    = -45.0f;
static constexpr float FULL_DB    = -6.0f;
static constexpr float ATTACK_DB  = 6.0f;
static constexpr float HOP_S      = AttackDetector::HOP / FS;

struct Event {
  double t;
  enum Kind { PLUCK, GLIDE, MUTE } kind;
  float midi;
  float peak;
  int   string = 0;   // two strings can ring at once
};

struct Result {
  std::vector<double> attacks;     // seconds
  std::vector<float>  strengths;   // normalized level at each attack
  std::vector<float>  level;       // normalized level per hop
};

static float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

static Result run(const std::vector<Event>& events, double seconds) {
  PluckSynth       strings[2];
  EnvelopeDetector env;
  AttackDetector   att;
  strings[0].configure(FS);
  strings[1].configure(FS);
  env.configure(FS, 2.0f, 120.0f);
  att.configure(FS);

  Result   r;
  float    buf[AttackDetector::HOP], other[AttackDetector::HOP];
  size_t   next = 0;
  uint32_t rng  = 7;
  double   t    = 0.0;
  for (size_t h = 0; t < seconds; h++, t = h * HOP_S) {
    while (next < events.size() && events[next].t <= t) {
      const Event& e = events[next++];
      PluckSynth&  s = strings[e.string];
      if (e.kind == Event::PLUCK) s.pluck(hz(e.midi), e.peak);
      if (e.kind == Event::GLIDE) s.glide(hz(e.midi));
      if (e.kind == Event::MUTE)  s.mute();
    }
    strings[0].render(buf, AttackDetector::HOP);
    strings[1].render(other, AttackDetector::HOP);
    for (size_t i = 0; i < AttackDetector::HOP; i++) {
      buf[i] += other[i];
      rng = rng * 1664525u + 1013904223u;
      const double ts = t + i / FS;
      buf[i] += 3.0f * ((int32_t)rng / 2147483648.0f)          // ADC noise, +-3 LSB
              + 1.5f * (float)std::sin(2.0 * M_PI * 50.0 * ts);  // mains hum
    }
    env.process(buf, AttackDetector::HOP);
    const float db  = EnvelopeDetector::toDb(env.envelope(), FULL_SCALE);
    const float lvl = EnvelopeDetector::normalize(db, GATE_DB, FULL_DB);
    if (att.processHop(buf, db, ATTACK_DB, db > GATE_DB)) {
      r.attacks.push_back(t);
      r.strengths.push_back(lvl);
    }
    r.level.push_back(lvl);
  }
  return r;
}

static float maxLevel(const Result& r, double from, double to) {
  float m = 0.0f;
  for (size_t i = (size_t)(from / HOP_S); i < r.level.size() && i * HOP_S < to; i++) {
    if (r.level[i] > m) m = r.level[i];
  }
  return m;
}

// Every expected pluck time has exactly one attack within 12 ms after it.
static void assertAttacksAt(const Result& r, const std::vector<double>& times) {
  char msg[96];
  snprintf(msg, sizeof(msg), "expected %zu attacks, got %zu", times.size(), r.attacks.size());
  TEST_ASSERT_EQUAL_MESSAGE(times.size(), r.attacks.size(), msg);
  for (size_t i = 0; i < times.size(); i++) {
    const double lag = r.attacks[i] - times[i];
    snprintf(msg, sizeof(msg), "attack %zu at %.3f s, pluck at %.3f s", i, r.attacks[i], times[i]);
    TEST_ASSERT_TRUE_MESSAGE(lag >= -HOP_S && lag <= 0.012, msg);
  }
}

void test_silence_gives_no_level_and_no_attacks() {
  Result r = run({}, 2.0);
  TEST_ASSERT_EQUAL(0, r.attacks.size());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, maxLevel(r, 0.0, 2.0));
}

void test_soft_pick_low_level_hard_pick_high_level() {
  const float soft = maxLevel(run({{0.1, Event::PLUCK, 45, 40}}, 0.6), 0.1, 0.6);
  const float hard = maxLevel(run({{0.1, Event::PLUCK, 45, 600}}, 0.6), 0.1, 0.6);
  TEST_ASSERT_TRUE_MESSAGE(soft > 0.1f && soft < 0.45f, "soft pick level out of range");
  TEST_ASSERT_TRUE_MESSAGE(hard > 0.85f, "hard pick level too low");
}

void test_varied_notes_and_strengths_one_attack_each() {
  const float  notes[]  = {40, 47, 52, 59, 64, 71, 76, 45};
  const float  peaks[]  = {600, 250, 450, 300, 550, 260, 400, 500};
  const double gaps[]   = {0.30, 0.45, 0.35, 0.60, 0.30, 0.50, 0.40, 0.35};
  std::vector<Event>  ev;
  std::vector<double> times;
  double t = 0.1;
  for (int i = 0; i < 8; i++) {
    ev.push_back({t, Event::PLUCK, notes[i], peaks[i]});
    times.push_back(t);
    t += gaps[i];
  }
  assertAttacksAt(run(ev, t + 0.5), times);
}

void test_repicking_a_ringing_string() {
  std::vector<Event>  ev;
  std::vector<double> times;
  for (int i = 0; i < 6; i++) {
    ev.push_back({0.1 + 0.25 * i, Event::PLUCK, 45, 400});
    times.push_back(0.1 + 0.25 * i);
  }
  assertAttacksAt(run(ev, 2.0), times);
}

void test_fast_picking_100ms() {
  std::vector<Event>  ev;
  std::vector<double> times;
  for (int i = 0; i < 10; i++) {
    ev.push_back({0.1 + 0.1 * i, Event::PLUCK, 52, 400});
    times.push_back(0.1 + 0.1 * i);
  }
  assertAttacksAt(run(ev, 1.5), times);
}

// Same note re-picked at equal strength while it still rings: the case where
// neither the level nor the high band changes much.
void test_fast_picking_high_notes() {
  for (float note : {64.0f, 76.0f}) {
    std::vector<Event>  ev;
    std::vector<double> times;
    for (int i = 0; i < 8; i++) {
      ev.push_back({0.1 + 0.1 * i, Event::PLUCK, note, 450});
      times.push_back(0.1 + 0.1 * i);
    }
    assertAttacksAt(run(ev, 1.3), times);
  }
}

void test_vibrato_does_not_trigger() {
  std::vector<Event> ev = {{0.1, Event::PLUCK, 57, 500}};
  for (double t = 0.3; t < 1.8; t += HOP_S) {
    ev.push_back({t, Event::GLIDE, 57.0f + 0.3f * (float)std::sin(2.0 * M_PI * 5.5 * t), 0});
  }
  assertAttacksAt(run(ev, 2.0), {0.1});
}

void test_second_string_while_first_rings() {
  std::vector<Event> ev = {
    {0.10, Event::PLUCK, 40, 550, 0},   // low E rings on
    {0.60, Event::PLUCK, 55, 300, 1},   // G on another string, softer
    {1.10, Event::PLUCK, 59, 300, 1},
    {1.60, Event::PLUCK, 45, 450, 0},
  };
  assertAttacksAt(run(ev, 2.2), {0.10, 0.60, 1.10, 1.60});
}

void test_sustained_note_triggers_once() {
  assertAttacksAt(run({{0.1, Event::PLUCK, 40, 600}}, 3.5), {0.1});
}

void test_slide_does_not_trigger() {
  std::vector<Event> ev = {{0.1, Event::PLUCK, 45, 500}};
  for (int i = 1; i <= 20; i++) ev.push_back({0.4 + 0.016 * i, Event::GLIDE, 45.0f + 0.6f * i, 0});
  assertAttacksAt(run(ev, 1.5), {0.1});
}

void test_harder_pick_gives_stronger_attack() {
  Result r = run({{0.1, Event::PLUCK, 45, 120}, {1.2, Event::PLUCK, 45, 600}}, 1.6);
  TEST_ASSERT_EQUAL(2, r.attacks.size());
  TEST_ASSERT_TRUE(r.strengths[1] > r.strengths[0] + 0.2f);
}

void test_level_falls_after_mute() {
  Result r = run({{0.1, Event::PLUCK, 40, 600}, {0.4, Event::MUTE, 0, 0}}, 1.4);
  TEST_ASSERT_TRUE_MESSAGE(maxLevel(r, 0.3, 0.4) > 0.7f, "ringing note level too low before mute");
  TEST_ASSERT_TRUE_MESSAGE(maxLevel(r, 1.2, 1.4) < 0.05f, "level did not fall within 0.8 s of mute");
  TEST_ASSERT_EQUAL(1, r.attacks.size());   // muting is not an attack
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_silence_gives_no_level_and_no_attacks);
  RUN_TEST(test_soft_pick_low_level_hard_pick_high_level);
  RUN_TEST(test_varied_notes_and_strengths_one_attack_each);
  RUN_TEST(test_repicking_a_ringing_string);
  RUN_TEST(test_fast_picking_100ms);
  RUN_TEST(test_fast_picking_high_notes);
  RUN_TEST(test_vibrato_does_not_trigger);
  RUN_TEST(test_second_string_while_first_rings);
  RUN_TEST(test_sustained_note_triggers_once);
  RUN_TEST(test_slide_does_not_trigger);
  RUN_TEST(test_harder_pick_gives_stronger_attack);
  RUN_TEST(test_level_falls_after_mute);
  return UNITY_END();
}
