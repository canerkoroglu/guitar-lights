/*
 * Host unit tests for Phase 4 pitch detection (YIN + PitchTracker),
 * through the same AudioAnalyzer the firmware runs.
 *
 *   pio test -e native
 */
#include <unity.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "AudioAnalyzer.h"
#include "PluckSynth.h"

static constexpr float  FS    = 15937.5f;   // actual rate on the ESP32
static constexpr float  FULL  = 927.0f;     // LSB swing from 1.65 V bias to 2.45 V
static constexpr size_t BLOCK = AudioAnalyzer::PITCH_HOP;   // firmware block size

struct Event {
  double t;
  enum Kind { PLUCK, GLIDE, MUTE } kind;
  float  midi;
  float  peak;
  int    string = 0;
  float  fund   = 1.0f;   // fundamental strength (0 = missing)
  float  inharm = 0.0f;   // string stiffness B
};

struct Frame {
  double t;
  bool   valid;
  int    note;
  float  midi, cents;
};

static float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

static std::string nameOf(int m) {
  static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return m < 0 ? "none" : std::string(n[m % 12]) + std::to_string(m / 12 - 1);
}

static std::vector<Frame> run(const std::vector<Event>& events, double seconds, float noise = 3.0f) {
  PluckSynth     strings[2];
  AudioAnalyzer  an;
  AnalyzerParams p;
  strings[0].configure(FS);
  strings[1].configure(FS);
  an.configure(FS, FULL);

  std::vector<Frame> frames;
  float    a[BLOCK], b[BLOCK];
  size_t   next = 0;
  uint32_t rng  = 11;
  for (size_t k = 0;; k++) {
    const double t = k * BLOCK / FS;
    if (t >= seconds) break;
    while (next < events.size() && events[next].t <= t) {
      const Event& e = events[next++];
      PluckSynth&  s = strings[e.string];
      if (e.kind == Event::PLUCK) { s.setTone(e.fund, e.inharm); s.pluck(hz(e.midi), e.peak); }
      if (e.kind == Event::GLIDE) s.glide(hz(e.midi));
      if (e.kind == Event::MUTE)  s.mute();
    }
    strings[0].render(a, BLOCK);
    strings[1].render(b, BLOCK);
    for (size_t i = 0; i < BLOCK; i++) {
      rng = rng * 1664525u + 1013904223u;
      a[i] += b[i] + noise * ((int32_t)rng / 2147483648.0f)
            + 1.5f * (float)std::sin(2.0 * M_PI * 50.0 * (t + i / FS));
    }
    an.process(a, BLOCK, p);
    const PitchState& ps = an.output().pitch;
    frames.push_back({t + BLOCK / FS, ps.valid, ps.midiNote, ps.midi, ps.cents});
  }
  return frames;
}

static std::vector<Frame> between(const std::vector<Frame>& f, double t0, double t1) {
  std::vector<Frame> out;
  for (const Frame& x : f) if (x.t >= t0 && x.t < t1) out.push_back(x);
  return out;
}

// Every frame in [t0, t1) is valid, on the expected note, within maxCents.
static void assertSteady(const std::vector<Frame>& f, double t0, double t1, int note,
                         float maxCents, float centsOffset = 0.0f) {
  char msg[160];
  for (const Frame& x : between(f, t0, t1)) {
    snprintf(msg, sizeof(msg), "t=%.3f expected %s, got %s (valid=%d, %.1f cents)",
             x.t, nameOf(note).c_str(), nameOf(x.note).c_str(), x.valid, x.cents);
    TEST_ASSERT_TRUE_MESSAGE(x.valid && x.note == note, msg);
    TEST_ASSERT_TRUE_MESSAGE(std::fabs(x.cents - centsOffset) <= maxCents, msg);
  }
}

// No valid frame ever reports a note outside `allowed`.
static void assertOnly(const std::vector<Frame>& f, std::vector<int> allowed) {
  char msg[96];
  for (const Frame& x : f) {
    if (!x.valid) continue;
    bool ok = false;
    for (int a : allowed) ok |= (x.note == a);
    snprintf(msg, sizeof(msg), "t=%.3f reported %s, which was not played", x.t, nameOf(x.note).c_str());
    TEST_ASSERT_TRUE_MESSAGE(ok, msg);
  }
}

static double firstOn(const std::vector<Frame>& f, double after, int note) {
  for (const Frame& x : f) if (x.t > after && x.valid && x.note == note) return x.t;
  return 1e9;
}

static int noteChanges(const std::vector<Frame>& f) {
  int changes = 0, last = -1;
  for (const Frame& x : f) {
    if (!x.valid) continue;
    if (last >= 0 && x.note != last) changes++;
    last = x.note;
  }
  return changes;
}

// ---------------------------------------------------------------------------

void test_open_strings() {
  for (int m : {40, 45, 50, 55, 59, 64}) {   // E2 A2 D3 G3 B3 E4
    auto f = run({{0.1, Event::PLUCK, (float)m, 450}}, 1.0);
    assertSteady(f, 0.2, 0.95, m, 10.0f);
  }
}

void test_chromatic_E2_to_E5() {
  for (int m = 40; m <= 76; m++) {
    auto f = run({{0.1, Event::PLUCK, (float)m, 450}}, 0.5);
    assertSteady(f, 0.2, 0.5, m, 12.0f);
  }
}

void test_top_of_the_neck_up_to_E6() {
  for (int m : {81, 84, 88}) {               // A5, C6, E6 (24th fret)
    auto f = run({{0.1, Event::PLUCK, (float)m, 450}}, 0.5);
    assertSteady(f, 0.2, 0.5, m, 20.0f);
  }
}

// The 2nd and 3rd harmonics louder than the fundamental: no octave error.
void test_weak_fundamental_no_octave_error() {
  for (int m : {40, 45, 50}) {
    Event e = {0.1, Event::PLUCK, (float)m, 450};
    e.fund = 0.1f;
    assertSteady(run({e}, 1.0), 0.2, 0.95, m, 10.0f);
  }
}

void test_missing_fundamental() {
  for (int m : {40, 45, 50}) {
    Event e = {0.1, Event::PLUCK, (float)m, 450};
    e.fund = 0.0f;
    assertSteady(run({e}, 1.0), 0.2, 0.95, m, 10.0f);
  }
}

// Stiff strings: overtones run sharp, so the waveform is not exactly
// periodic. The reading is a little sharp right after the pick, while the
// bright overtones are strong, and settles as they decay - always the
// right note.
void test_inharmonic_strings() {
  for (int m : {40, 45, 50}) {
    Event typical = {0.1, Event::PLUCK, (float)m, 450};
    typical.inharm = 1e-4f;                    // typical wound string
    assertSteady(run({typical}, 1.0), 0.2, 0.95, m, 10.0f);

    Event stiff = typical;
    stiff.inharm = 2e-4f;                      // very stiff string
    auto f = run({stiff}, 1.0);
    assertSteady(f, 0.2, 0.95, m, 20.0f);
    assertSteady(f, 0.6, 0.95, m, 5.0f);
  }
}

void test_out_of_tune_notes_keep_their_name() {
  assertSteady(run({{0.1, Event::PLUCK, 45.4f, 450}}, 1.0), 0.2, 0.95, 45, 10.0f, +40.0f);
  assertSteady(run({{0.1, Event::PLUCK, 44.6f, 450}}, 1.0), 0.2, 0.95, 45, 10.0f, -40.0f);
}

void test_noise_gives_no_pitch() {
  auto f = run({}, 2.0, 40.0f);   // loud broadband noise, well above the gate
  for (const Frame& x : f) TEST_ASSERT_FALSE_MESSAGE(x.valid, "noise produced a pitch");
}

void test_latency_from_silence() {
  for (int m : {40, 45, 50, 55, 64}) {
    auto f = run({{0.1, Event::PLUCK, (float)m, 450}}, 0.5);
    const double lag = firstOn(f, 0.1, m) - 0.1;
    char msg[64];
    snprintf(msg, sizeof(msg), "%s took %.0f ms", nameOf(m).c_str(), lag * 1000);
    TEST_ASSERT_TRUE_MESSAGE(lag <= 0.100, msg);   // readme target: < 100 ms
  }
}

void test_note_change_is_clean_and_fast() {
  auto f = run({{0.1, Event::PLUCK, 45, 450}, {0.6, Event::PLUCK, 50, 450}}, 1.2);
  const double lag = firstOn(f, 0.6, 50) - 0.6;
  char msg[64];
  snprintf(msg, sizeof(msg), "A2 -> D3 took %.0f ms", lag * 1000);
  TEST_ASSERT_TRUE_MESSAGE(lag <= 0.090, msg);
  assertOnly(f, {45, 50});
  TEST_ASSERT_EQUAL(1, noteChanges(f));
}

void test_vibrato_does_not_flicker() {
  std::vector<Event> ev = {{0.1, Event::PLUCK, 45, 450}};
  for (double t = 0.2; t < 1.6; t += BLOCK / FS) {
    ev.push_back({t, Event::GLIDE, 45.0f + 0.3f * (float)std::sin(2.0 * M_PI * 5.5 * t), 0});
  }
  auto f = run(ev, 1.6);
  assertSteady(f, 0.25, 1.6, 45, 45.0f);
  TEST_ASSERT_EQUAL(0, noteChanges(f));
}

void test_slide_moves_through_notes_in_order() {
  std::vector<Event> ev = {{0.1, Event::PLUCK, 45, 450}};
  for (double t = 0.4; t <= 0.7; t += BLOCK / FS) {
    ev.push_back({t, Event::GLIDE, 45.0f + 4.0f * (float)((t - 0.4) / 0.3), 0});
  }
  auto f = run(ev, 1.1);
  assertOnly(f, {45, 46, 47, 48, 49});
  int last = 0;
  for (const Frame& x : f) {
    if (!x.valid) continue;
    TEST_ASSERT_TRUE_MESSAGE(x.note >= last, "slide went backwards");
    last = x.note;
  }
  assertSteady(f, 0.85, 1.1, 49, 15.0f);
}

// Low E still ringing when a B3 is picked on another string: no invented notes.
void test_two_strings_no_garbage() {
  auto f = run({{0.1, Event::PLUCK, 40, 500, 0}, {0.8, Event::PLUCK, 59, 450, 1}}, 1.6);
  assertOnly(f, {40, 59});
  TEST_ASSERT_TRUE(noteChanges(f) <= 1);
}

void test_pitch_drops_after_mute() {
  auto f = run({{0.1, Event::PLUCK, 45, 450}, {0.6, Event::MUTE, 0, 0}}, 1.6);
  assertSteady(f, 0.2, 0.6, 45, 10.0f);
  for (const Frame& x : between(f, 1.4, 1.6)) TEST_ASSERT_FALSE_MESSAGE(x.valid, "pitch still valid after mute");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_open_strings);
  RUN_TEST(test_chromatic_E2_to_E5);
  RUN_TEST(test_top_of_the_neck_up_to_E6);
  RUN_TEST(test_weak_fundamental_no_octave_error);
  RUN_TEST(test_missing_fundamental);
  RUN_TEST(test_inharmonic_strings);
  RUN_TEST(test_out_of_tune_notes_keep_their_name);
  RUN_TEST(test_noise_gives_no_pitch);
  RUN_TEST(test_latency_from_silence);
  RUN_TEST(test_note_change_is_clean_and_fast);
  RUN_TEST(test_vibrato_does_not_flicker);
  RUN_TEST(test_slide_moves_through_notes_in_order);
  RUN_TEST(test_two_strings_no_garbage);
  RUN_TEST(test_pitch_drops_after_mute);
  return UNITY_END();
}
