/*
 * Guitar Reactive RGB LED System — ESP32-WROOM-32
 * ------------------------------------------------------------------
 * Current stage: Phase 1 analog guitar input (+ Phase 0 LEDs, Wi-Fi portal).
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
 * guitar) -> DC removal -> AudioDsp (Phase 2 VU level, Phase 3 attacks)
 * -> AudioFeatures -> EffectEngine. Pitch detection is Phase 4; until
 * then the simulated guitar passes through the note it is playing.
 *
 * USB serial commands (115200 baud, one per line):
 *   {json}  apply settings, same format as POST /api/config
 *   cfg     print current settings
 *   leds    print the LED buffer as hex, LED 0 first
 *   pluck   pick the simulated guitar once
 *   feat    print audio features (level, dB, attacks, pitch)
 *   adc     print guitar input statistics (GPIO34)
 *   scope   print the latest 512 raw ADC samples (blocks loop ~0.2 s)
 *   tone on / tone off   DAC test tone on GPIO26 for the loopback test
 *
 * Wiring (readme.md pin plan):
 *   ESP32 GPIO18 --[330ohm]--> WS2812B DIN   (resistor near first LED)
 *   External 5V -------------> WS2812B 5V     (1000uF across 5V/GND)
 *   GND (common with ESP32) -> WS2812B GND
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

CRGB         leds[NUM_LEDS];
EffectEngine engine;

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
                    "pitch=%d midi=%.2f vel=%.1f\n",
                    d.level, d.db, d.rise, (unsigned)d.attacks, d.attackStrength,
                    d.pitchValid, d.midi, d.velocity);
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
    } else if (line.length()) {
      Serial.println(F("commands: {json} | cfg | leds | pluck | feat | adc | scope | tone on | tone off"));
    }
    line = "";
  }
}

void setup() {
  Serial.setRxBufferSize(1024);   // full settings JSON fits in one line
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("== Guitar RGB : Phase 1 guitar input + web control =="));
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

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);

  webPortalBegin(leds, NUM_LEDS);

  if (AudioInput::begin()) {
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

  const uint32_t now = millis();
  static uint32_t lastFrame = 0;
  static uint32_t lastBeat  = 0;

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    const Settings& s = ConfigStore::get();

    // Attacks arrive as a counter from the audio task; an attack is "new"
    // for exactly one frame (the refractory period is longer than a frame).
    static uint32_t seenAttacks = 0;
    const AudioDsp::Snapshot d = AudioDsp::snapshot();
    AudioFeatures audio;
    audio.level          = d.level;
    audio.attackDetected = d.attacks != seenAttacks;
    audio.attackStrength = d.attackStrength;
    audio.pitchValid     = d.pitchValid;
    audio.midiNote       = d.midi;
    audio.pitchVelocity  = d.velocity;
    seenAttacks = d.attacks;

    engine.render(s, now, audio);
    FastLED.setBrightness(s.power ? s.brightness : 0);
    FastLED.show();
  }

  if (now - lastBeat >= HEARTBEAT_MS) {
    lastBeat = now;
    const AudioInput::Stats a = AudioInput::stats();
    Serial.printf("[hb] up=%lus clients=%d heap=%u | adc %.0fHz dc=%.2fV p2p=%.3fV clip=%u\n",
                  now / 1000UL, WiFi.softAPgetStationNum(), ESP.getFreeHeap(),
                  a.sampleRate, a.dcV, a.p2pV, (unsigned)a.clipped);
  }
}
