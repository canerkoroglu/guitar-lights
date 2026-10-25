/*
 * Guitar Reactive RGB LED System — ESP32-WROOM-32
 * ------------------------------------------------------------------
 * Current stage: Phases 0-4 (LEDs, Wi-Fi portal, guitar input, VU level,
 * attacks, pitch detection), the effect engine with presets, the Phase 8
 * mode button with tuner, and the Phase 9 battery monitor.
 *
 * Boot:
 *   1. Quick LED self test (Red -> Green -> Blue -> Off).
 *   2. Load settings from NVS (or defaults).
 *   3. Start Wi-Fi access point "GuitarRGB" and the web control page.
 *
 * Connect a phone/laptop to the "GuitarRGB" Wi-Fi network, then open
 * http://192.168.4.1/  (or http://guitar.local/ on macOS/iOS).
 *
 * Audio path: AudioInput (I2S ADC, or GuitarSynth when Source = Simulated
 * guitar) -> DC removal -> AudioDsp (Phase 2 VU level, Phase 3 attacks,
 * Phase 4 pitch) -> AudioFeatures -> EffectEngine. The simulated guitar's
 * own note is only reported alongside (feat "truth") to check detection.
 *
 * USB serial commands (115200 baud, one per line):
 *   {json}  apply settings, same format as POST /api/config
 *   cfg     print current settings
 *   presets list the presets;  preset N  apply one
 *   leds    print the LED buffer as hex, LED 0 first
 *   pluck   pick the simulated guitar once
 *   feat    print audio features (level, dB, attacks, pitch)
 *   adc     print guitar input statistics (GPIO34)
 *   scope   print the latest 512 raw ADC samples (blocks loop ~0.2 s)
 *   tone on / tone off   DAC test tone on GPIO26 for the loopback test
 *   tuner on / tuner off
 *   bat     print battery monitor state (pin mV, cell V, %, state)
 *   press short | press long | press hold   act as the mode button
 *
 * Wiring (readme.md pin plan):
 *   ESP32 GPIO18 --[330ohm]--> WS2812B DIN   (resistor near first LED)
 *   External 5V -------------> WS2812B 5V     (1000uF across 5V/GND)
 *   GND (common with ESP32) -> WS2812B GND
 *   ESP32 GPIO25 --[button]--> GND            (mode button, internal pull-up)
 *   Cell + --[100k]-- GPIO35 --[100k]-- GND   (optional battery monitor,
 *                                              100nF from GPIO35 to GND)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include "Config.h"
#include "AudioFeatures.h"
#include "AudioDsp.h"
#include "GuitarSynth.h"
#include "AudioInput.h"
#include "TestTone.h"
#include "EffectEngine.h"
#include "WebPortal.h"
#include "Presets.h"
#include "Stage.h"
#include "Battery.h"

// ---- Hardware ----
static constexpr uint8_t  LED_DATA_PIN = 18;   // GPIO18 -> WS2812B DIN
static constexpr uint16_t NUM_LEDS     = 16;
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

// ---- Wi-Fi access point (password must be >= 8 characters) ----
static const char* AP_SSID   = "GuitarRGB";
static const char* AP_PASS   = "guitarled";
static const char* MDNS_NAME = "guitar";       // http://guitar.local/

static constexpr uint32_t FRAME_MS     = 16;   // ~60 FPS
static constexpr uint32_t HEARTBEAT_MS = 5000;

// Tuner and battery gauge stay visible even at very low brightness or with
// the lights switched off; a critical battery caps brightness to save it.
static constexpr uint8_t STAGE_MIN_BRIGHTNESS = 24;
static constexpr uint8_t CRITICAL_BRIGHTNESS  = 48;

CRGB         leds[NUM_LEDS];
EffectEngine engine;
static bool  audioRunning = false;   // AudioInput::begin() succeeded

static void quickSelfTest() {
  const CRGB seq[] = {CRGB::Red, CRGB::Green, CRGB::Blue};
  for (const CRGB& c : seq) {
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(200);   // one-time boot test only; the main loop never blocks
  }
  FastLED.clear(true);
}

static void printAdc() {
  const AudioInput::Stats a = AudioInput::stats();
  Serial.printf("adc running=%d rate=%.0f dc=%.3f min=%.3f max=%.3f p2p=%.3f rms=%.4f "
                "clip=%u zc=%.1f lo=%u hi=%u tone=%d\n",
                a.running, a.sampleRate, a.dcV, a.minV, a.maxV, a.p2pV, a.rmsV,
                (unsigned)a.clipped, a.zeroCrossHz, a.rawLo, a.rawHi, TestTone::active());
}

// Latest DSP results as the effect engine sees them. Attacks arrive as a
// counter from the audio task; an attack is "new" for exactly one frame
// (the refractory period is longer than a frame).
static AudioFeatures readAudioFeatures() {
  static uint32_t seenAttacks = 0;
  const AudioDsp::Snapshot d = AudioDsp::snapshot();
  AudioFeatures a;
  a.level          = d.level;
  a.attackDetected = d.attacks != seenAttacks;
  a.attackStrength = d.attackStrength;
  a.pitchValid     = d.pitchValid;
  a.midiNote       = d.midi;
  a.note           = d.note;
  a.cents          = d.cents;
  a.pitchVelocity  = d.velocity;
  seenAttacks = d.attacks;
  return a;
}

// The audio task completes a block every 16 ms. If it stops for 1.5 s the
// LEDs would freeze, so restart the board (back in ~2 s). If the stall
// followed a runtime battery read, stop doing those first: the I2S ADC
// hand-over is the one driver path the firmware cannot fully prove.
static void audioWatchdog(uint32_t now) {
  static constexpr uint32_t STALL_MS = 1500;
  static uint32_t lastBlocks = 0, lastProgress = 0;
  if (!audioRunning) return;
  const uint32_t b = AudioInput::blockCount();
  if (b != lastBlocks || lastProgress == 0) {
    lastBlocks   = b;
    lastProgress = now;
    return;
  }
  if (now - lastProgress < STALL_MS) return;

  const uint32_t readAt = AudioInput::lastBatteryReadMs();
  const bool afterBatteryRead = readAt && now - readAt < STALL_MS + 1000;
  Serial.printf("[watchdog] audio stalled%s, restarting\n", afterBatteryRead ? " after a battery read" : "");
  if (afterBatteryRead) Battery::disableRuntimeReads();
  ConfigStore::save();   // keep a change made in the last 2 s
  Serial.flush();
  ESP.restart();
}

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (line.length() < 1024) line += ch;
      continue;
    }
    line.trim();
    if (line.startsWith("{")) {
      String err;
      if (ConfigStore::fromJson(line, err)) {
        ConfigStore::save();
        Serial.println(F("ok"));
      } else {
        Serial.printf("error %s\n", err.c_str());
      }
    } else if (line == "cfg") {
      Serial.println(ConfigStore::toJson());
    } else if (line == "presets") {
      for (size_t i = 0; i < Presets::count(); i++) Serial.printf("%u %s\n", (unsigned)i, Presets::name(i));
    } else if (line.startsWith("preset ")) {
      size_t i = 0;
      if (Presets::parseIndex(line.c_str() + 7, i) && Presets::apply(i)) {
        Serial.printf("preset %s\n", Presets::name(i));
      } else {
        Serial.println(F("error no such preset"));
      }
    } else if (line == "leds") {
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        Serial.printf("%02x%02x%02x ", leds[i].r, leds[i].g, leds[i].b);
      }
      Serial.println();
    } else if (line == "pluck") {
      GuitarSynth::pluck();
      Serial.println(F("ok"));
    } else if (line == "feat") {
      const AudioDsp::Snapshot d = AudioDsp::snapshot();
      Serial.printf("feat level=%.3f db=%.1f rise=%.1f attacks=%u strength=%.2f "
                    "pitch=%d note=%d midi=%.2f cents=%.1f conf=%.2f vel=%.1f truth=%.2f cpu=%.1f\n",
                    d.level, d.db, d.rise, (unsigned)d.attacks, d.attackStrength,
                    d.pitchValid, d.note, d.midi, d.cents, d.confidence, d.velocity,
                    d.truthMidi, d.cpu);
    } else if (line == "adc") {
      printAdc();
    } else if (line == "scope") {
      static uint16_t s[AudioInput::SCOPE_LEN];
      const size_t n = AudioInput::scope(s, AudioInput::SCOPE_LEN);
      for (size_t i = 0; i < n; i++) Serial.printf("%u ", s[i]);
      Serial.println();
    } else if (line == "tone on" || line == "tone off") {
      TestTone::set(line == "tone on");
      Serial.println(F("ok"));
    } else if (line == "tuner on" || line == "tuner off") {
      Stage::setTuner(line == "tuner on");
      Serial.println(F("ok"));
    } else if (line == "bat") {
      const Battery::Status b = Battery::status();
      Serial.printf("bat fitted=%d pin=%umV cell=%.3fV pct=%d state=%s%s\n", b.fitted, (unsigned)b.pinMv,
                    b.volts, b.percent, BatteryMonitor::name(b.state), b.bootOnly ? " (boot reading only)" : "");
    } else if (line == "press short" || line == "press long" || line == "press hold") {
      const ButtonGesture::Event e = line == "press short" ? ButtonGesture::SHORT_PRESS
                                   : line == "press long"  ? ButtonGesture::LONG_PRESS
                                                           : ButtonGesture::HOLD;
      Stage::handle(e, millis());
      Serial.println(F("ok"));
    } else if (line.length()) {
      Serial.println(F("commands: {json} | cfg | presets | preset N | leds | pluck | feat | adc | scope | "
                       "tone on|off | tuner on|off | bat | press short|long|hold"));
    }
    line = "";
  }
}

void setup() {
  Serial.setRxBufferSize(1024);   // full settings JSON fits in one line
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("== Guitar RGB : guitar input, audio analysis + web control =="));
  Serial.printf("Chip %s rev %d, %d cores @ %u MHz, heap %u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                ESP.getCpuFreqMHz(), ESP.getFreeHeap());

  FastLED.addLeds<LED_TYPE, LED_DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(64);
  FastLED.clear(true);
  quickSelfTest();

  ConfigStore::begin();
  engine.begin(leds, NUM_LEDS);
  Stage::begin();
  Battery::begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);

  webPortalBegin(leds, NUM_LEDS);

  audioRunning = AudioInput::begin();
  if (audioRunning) {
    Serial.printf("Guitar input: I2S ADC on GPIO%d @ %u Hz\n",
                  AudioInput::ADC_GPIO, (unsigned)AudioInput::SAMPLE_RATE);
  } else {
    Serial.println(F("Guitar input: FAILED to start I2S ADC"));
  }

  Serial.printf("Wi-Fi AP \"%s\"  password \"%s\"\n", AP_SSID, AP_PASS);
  Serial.printf("Open http://%s/  or  http://%s.local/\n",
                WiFi.softAPIP().toString().c_str(), MDNS_NAME);
}

void loop() {
  webPortalLoop();
  handleSerial();
  ConfigStore::loop();   // deferred settings save

  const uint32_t now = millis();
  Stage::loop(now);                          // mode button
  Battery::loop(ConfigStore::get(), now);
  audioWatchdog(now);
  static uint32_t lastFrame = 0;
  static uint32_t lastBeat  = 0;

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    const Settings&       s     = ConfigStore::get();
    const AudioFeatures   audio = readAudioFeatures();   // every frame: consumes attacks
    const Battery::Status bat   = Battery::status();

    bool stageScreen = true;
    if (Stage::gaugeActive(now)) {
      engine.renderBatteryGauge(s, now, Stage::gaugeStartedAt(), bat.fitted ? bat.percent : -1);
    } else if (Stage::tuner()) {
      engine.renderTuner(s, now, audio);
    } else {
      engine.render(s, now, audio);
      stageScreen = false;
    }
    if (bat.fitted) engine.overlayBatteryWarning(s, now, bat.state);

    uint8_t brightness = s.power ? s.brightness : 0;
    if (stageScreen) brightness = max(s.brightness, STAGE_MIN_BRIGHTNESS);
    if (bat.fitted && bat.state == BatteryState::Critical) brightness = min(brightness, CRITICAL_BRIGHTNESS);
    FastLED.setBrightness(brightness);
    FastLED.show();
  }

  if (now - lastBeat >= HEARTBEAT_MS) {
    lastBeat = now;
    const AudioInput::Stats a = AudioInput::stats();
    const Battery::Status b = Battery::status();
    Serial.printf("[hb] up=%lus clients=%d heap=%u | adc %.0fHz dc=%.2fV p2p=%.3fV clip=%u | dsp %.1f%%",
                  now / 1000UL, WiFi.softAPgetStationNum(), ESP.getFreeHeap(),
                  a.sampleRate, a.dcV, a.p2pV, (unsigned)a.clipped, AudioDsp::snapshot().cpu);
    if (b.fitted) Serial.printf(" | bat %.2fV %d%% %s", b.volts, b.percent, BatteryMonitor::name(b.state));
    Serial.println();
  }
}
