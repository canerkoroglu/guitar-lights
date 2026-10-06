# Guitar Reactive RGB LED System

## Project Overview

This project turns a DIY electric guitar into a real-time, audio-reactive RGB instrument.

The guitar has **16 WS2812B addressable RGB LEDs** installed on/in the body. An **ESP32-WROOM-32** reads the guitar's analog audio signal, analyzes it in real time, and generates visual effects.

The system should eventually react to:

- Guitar volume / amplitude (VU meter)
- Peaks and transients
- Rhythm / beat-like attacks
- Pitch / single-note detection
- Rising and falling pitch
- Low / mid / high frequency energy
- Sustain and decay
- Configurable LED animations and color mappings

The system is **not intended to recognize chords**. Single-note pitch detection is sufficient.

The primary audio source is the **guitar's own electrical output**, not an ambient microphone. This prevents room noise, drums, vocals, and other instruments from affecting the LED behavior.

---

# Hardware

## Main Components

### Controller

**Selected board: ESP32-WROOM-32 development board (ESP32 DevKit).**

- ESP32-WROOM-32 (ESP32 DevKit)
- CP2102 USB-UART development board
- Prefer the 38-pin development board if purchasing a new board, although 30-pin versions are sufficient for this project.
- Serial monitor / Serial Plotter baud rate: **115200** (ESP32 default used throughout this project).

### Board Selection / Compatibility

Two boards are available for this build:

| Board | Role | Suitable as main controller? |
|-------|------|------------------------------|
| **ESP32-WROOM-32 (ESP32 DevKit)** | Main audio DSP + LED controller | ✅ Yes — **selected** |
| NodeMCU ESP8266 (serial @ 9600 baud) | Optional / not required | ❌ No |

The **ESP32-WROOM-32 is the only compatible controller for this project.** The real-time audio pipeline (8–16 kHz sampling, RMS/envelope follower, autocorrelation/YIN pitch detection, optional FFT) running *simultaneously* with driving 16 WS2812B LEDs needs the ESP32's dual-core 240 MHz CPU, ~520 KB SRAM, and multiple 12-bit ADC channels — including input-only ADC pins such as GPIO34 used in the pin plan below.

The **NodeMCU ESP8266 is not suitable as the main controller** for this project, for the following hardware reasons:

- **ADC:** It exposes only **one ADC input (A0)** at 10-bit resolution. Its sampling rate is limited and is disturbed while Wi-Fi is active, so it cannot provide reliable continuous audio sampling for DSP.
- **CPU / RAM:** It is **single-core (80/160 MHz)** with far less usable RAM, which is marginal for real-time pitch detection and FFT.
- **LED timing:** Bit-banging the WS2812B data line requires interrupts to be disabled during each frame, which conflicts with the ESP8266's Wi-Fi/timing behaviour and risks LED flicker or Wi-Fi instability.

Because the ESP32-WROOM-32 already provides built-in Wi-Fi and Bluetooth, the ESP8266 is **not needed** for the core system or for the future wireless features. If desired, the ESP8266 could optionally be repurposed later as a **separate UART companion** (e.g. a telemetry or config bridge) talking to the ESP32 over serial at its **9600 baud** rate, but this is explicitly **outside the Phase 0–10 core system** and should not be added unless it solves a demonstrated problem.

### LEDs

- 16 × WS2812B addressable RGB LEDs
- 5V LED power
- Common GND between ESP32, audio circuit, and LED power supply

### Guitar

- DIY electric guitar
- Harley Benton guitar kit passive pickups
- Standard passive guitar electronics:
  - Pickups
  - Volume potentiometer
  - Tone potentiometer
  - Pickup selector
  - Output jack

The preferred audio tap point is the guitar output signal after the volume/control circuitry, so the ESP32 analyzes the actual guitar output.

---

# Audio Input Architecture

The guitar signal must NOT be connected directly to an ESP32 ADC pin without signal conditioning.

The guitar signal is an AC audio waveform and can swing below 0V. ESP32 ADC inputs cannot accept negative voltage.

The input stage should therefore:

1. Present a high impedance load to the passive guitar pickup/output.
2. AC-couple the signal.
3. Bias the signal around approximately 1.65V.
4. Keep the signal within the ESP32 ADC input range.
5. Optionally provide adjustable gain/filtering.

Initial architecture:

```text
Guitar Output
     |
     +---------------------> Amplifier
     |
     +--> High-Z Buffer
              |
           AC coupling
              |
        1.65V bias point
              |
          ESP32 ADC
```

## Buffer / Op-Amp

Selected: **MCP6022-I/P** (DIP-8, dual), powered from the ESP32's 3.3V pin.

Why:

- Runs from 2.5-5.5V and is rail-to-rail on input and output, so it works on a single 3.3V supply.
- Powered from 3.3V, its output cannot exceed the ESP32 ADC's input limit. The supply itself protects the ADC.
- CMOS inputs draw about 1pA, so a 2.2MΩ bias resistor works, keeping the load on the passive pickups light.
- Low noise (8.7 nV/√Hz) and 10MHz gain-bandwidth, far more than guitar audio needs. Unity-gain stable.
- Faster op-amps can ring when driving a capacitor directly. The 1kΩ resistor between the output and the 22nF anti-alias capacitor isolates that load; keep it.

Not used:

- TL072 / TL082 / NE5532 / OPA2134: need about 7V or more and are not rail-to-rail, so they cannot run from 3.3V. On a 9V supply their output could exceed the ESP32's 3.3V input limit and would need extra clamping.
- LM358: on 3.3V its output only reaches about 1.8V, leaving almost no headroom above the 1.65V bias.
- MCP6002: works in the same circuit (same pinout, 1.8-6V), but about 3x noisier with 1MHz bandwidth.

Do not assume an op-amp is automatically suitable just because it is available. Check supply voltage, input/output range, biasing requirements, and the actual circuit before implementation.

## ADC

Start with the **ESP32 internal ADC**.

Do NOT add ADS1115 or MCP3201 just because they have higher nominal resolution. They are not the preferred solution for this audio application.

The project does not require studio-quality audio recording.

A dedicated audio ADC such as PCM1808 could be considered later if the ESP32 ADC proves to be the limiting factor, but it is explicitly **not a requirement for Phase 1**.

---

# LED Signal Path

The ESP32 drives the WS2812B data line.

Recommended:

```text
ESP32 GPIO
   |
  330Ω
   |
WS2812B DIN
```

Place the 330Ω resistor close to the first LED.

Add a bulk capacitor across the LED supply:

```text
5V ----+---- WS2812B
       |
     1000µF
       |
GND ---+
```

For a more robust design, consider a 74AHCT125 / 74HCT14 level shifter for converting the ESP32's 3.3V logic to a 5V WS2812 data signal.

For only 16 LEDs, direct 3.3V data may work depending on LED type, wiring, and power conditions, but a level shifter is preferred for a production-quality installation.

---

# Important Electrical Rules

- Never feed a negative guitar waveform directly into an ESP32 ADC.
- Never feed more than the permitted ADC voltage into the ESP32.
- ESP32, audio circuit, and LED power supply must share a suitable common ground.
- Do not power 16 WS2812 LEDs from an ESP32 GPIO.
- LEDs need their own suitable 5V power source.
- Keep high-current LED wiring separate from the sensitive analog audio path where possible.
- Keep the analog audio wiring short and away from LED power/data wiring.
- Use a star/common grounding strategy where practical to reduce LED switching noise.
- Add local decoupling capacitors around the ESP32 and analog circuit.

---

# Project Goals

## Functional Goals

The final system should be able to:

### 1. VU Meter

LED brightness or LED position should follow guitar signal amplitude.

Example:

```text
Low volume     -> few/dim LEDs
Medium volume  -> more LEDs
High volume    -> many/bright LEDs
Peak           -> temporary flash/accent
```

### 2. Transient / Attack Detection

Detect strong changes in amplitude, such as:

- Pick attack
- Strong strum
- Palm-muted hit
- Accented note

These events can trigger short LED flashes or color changes.

### 3. Rhythm Response

Detect repeated attacks/transients and create rhythm-reactive effects.

This does not need to be a sophisticated music beat tracker. The system primarily needs to react musically to guitar attacks.

### 4. Pitch Detection

Detect the fundamental frequency of a **single guitar note**.

Chord recognition is explicitly out of scope.

Example:

```text
E2 ≈ 82.4 Hz
A2 ≈ 110.0 Hz
D3 ≈ 146.8 Hz
G3 ≈ 196.0 Hz
B3 ≈ 246.9 Hz
E4 ≈ 329.6 Hz
```

The implementation should convert detected frequency to a musical note and optionally MIDI note number.

### 5. Rising / Falling Pitch

Track pitch over time.

Example:

```text
E -> F# -> G -> A
```

can produce a rising LED animation.

Similarly:

```text
A -> G -> F# -> E
```

can produce a falling animation.

### 6. Frequency Energy

The system may analyze low/mid/high frequency energy to create effects.

This can be implemented with FFT or digital filters depending on CPU/memory requirements.

FFT is useful for spectrum analysis, but it is not mandatory for pitch detection.

---

# DSP Architecture

The audio processing pipeline should be modular.

Recommended architecture:

```text
ADC
 |
 v
Audio Buffer
 |
 +--> DC removal / normalization
 |
 +--> Amplitude / RMS
 |
 +--> Peak / transient detector
 |
 +--> Pitch detector
 |
 +--> Frequency-band analysis
 |
 v
Feature State
 |
 v
Effect Engine
 |
 v
WS2812 LEDs
```

## Sampling

Choose a suitable sample rate for guitar audio.

Start around:

- 8 kHz or
- 16 kHz

and benchmark CPU/memory usage.

For pitch detection of low guitar notes, use sufficiently large analysis windows.

The lowest standard guitar fundamental is around 82 Hz, so the algorithm must have enough observation time to distinguish low-frequency notes.

Do not optimize for minimum latency at the expense of unstable pitch detection.

---

# Pitch Detection

Pitch detection should be implemented separately from FFT.

Preferred algorithms to investigate:

1. Autocorrelation
2. YIN
3. McLeod Pitch Method (MPM)

Start with autocorrelation or YIN because the target is monophonic guitar notes.

Pitch detection should provide:

```cpp
struct PitchState {
    bool valid;
    float frequencyHz;
    float confidence;
    int midiNote;
    float cents;
};
```

Use confidence thresholds and smoothing to prevent LED flickering between incorrect notes.

Pitch detection should include:

- Noise gate
- Minimum amplitude threshold
- Confidence threshold
- Temporal smoothing
- Hysteresis
- Optional note hold time
- Optional attack/release behavior

---

# Audio Feature Model

Create a central state structure instead of letting each LED effect read raw ADC data independently.

Example:

```cpp
struct AudioFeatures {
    float rms;
    float peak;
    float normalizedLevel;

    bool attackDetected;
    float attackStrength;

    bool pitchValid;
    float frequencyHz;
    float pitchConfidence;
    int midiNote;
    float cents;

    float lowEnergy;
    float midEnergy;
    float highEnergy;

    float pitchVelocity;
};
```

Effects consume this state.

This keeps DSP separate from visualization.

---

# LED Effect Engine

The LED code should be independent from audio acquisition.

Recommended interface:

```cpp
class Effect {
public:
    virtual void update(const AudioFeatures& audio, uint32_t now) = 0;
};
```

Possible effects:

## Effect 1 - VU Meter

Map normalized amplitude to LED count.

## Effect 2 - Peak Flash

Normal VU meter with a short white/color flash when an attack is detected.

## Effect 3 - Pitch Color

Map musical notes or pitch classes to colors.

Example:

```text
C  -> red
C# -> orange
D  -> yellow
D# -> green
E  -> cyan
F  -> blue
F# -> violet
G  -> magenta
...
```

The exact palette should be configurable.

## Effect 4 - Pitch Position

Map pitch/frequency to LED position.

Low notes can illuminate one side and high notes the other.

## Effect 5 - Rising/Falling

Use pitch velocity:

```text
pitchVelocity > threshold
    -> rising animation

pitchVelocity < -threshold
    -> falling animation
```

## Effect 6 - Frequency Bands

Divide the 16 LEDs into low/mid/high regions.

Example:

```text
LED 0-4    -> low
LED 5-10   -> mid
LED 11-15  -> high
```

## Effect 7 - Sustain

When a note continues:

- attack = bright
- sustain = stable/dimmed
- release = gradual fade

---

# Software Architecture

Use a modular architecture.

Suggested project structure:

```text
src/
  main.cpp

  audio/
    AudioInput.cpp
    AudioInput.h
    AudioBuffer.cpp
    AudioBuffer.h

  dsp/
    AudioFeatures.cpp
    AudioFeatures.h
    PitchDetector.cpp
    PitchDetector.h
    EnvelopeDetector.cpp
    EnvelopeDetector.h
    FrequencyAnalyzer.cpp
    FrequencyAnalyzer.h

  leds/
    LedController.cpp
    LedController.h

  effects/
    Effect.h
    VuEffect.cpp
    VuEffect.h
    PitchEffect.cpp
    PitchEffect.h
    RhythmEffect.cpp
    RhythmEffect.h
    SpectrumEffect.cpp
    SpectrumEffect.h

  config/
    Config.h
```

Use PlatformIO if possible.

---

# Development Phases

## Phase 0 - Hardware Validation

Goal: Verify the hardware independently.

Tasks:

- Verify ESP32 programming.
- Verify 16 WS2812 LEDs.
- Verify LED power supply.
- Verify common ground.
- Verify LED data signal.
- Run a simple rainbow/chase test.

Acceptance criteria:

- All 16 LEDs work.
- No random flickering.
- ESP32 remains stable.

Do not start DSP work until this phase works.

---

## Phase 1 - Analog Guitar Input

Goal: Read the guitar waveform safely.

Tasks:

- Build high-impedance input buffer.
- Add AC coupling.
- Create 1.65V bias.
- Connect to an ESP32 ADC pin.
- Read raw ADC values.
- Remove DC offset in software.
- Plot/log ADC samples.

Acceptance criteria:

- Guitar signal is visible in Serial Plotter or equivalent.
- No clipping under normal guitar playing.
- No negative ADC readings.
- ADC input stays within safe limits.
- Amplifier output remains usable when ESP32 input is connected.
- Guitar tone is not audibly degraded.

Do not implement pitch detection yet.

---

## Phase 2 - VU Meter

Goal: Make LEDs respond to guitar amplitude.

Tasks:

- Calculate DC-corrected amplitude.
- Implement envelope follower / RMS.
- Normalize the signal.
- Implement attack and release smoothing.
- Map level to 16 LEDs.

Acceptance criteria:

- Soft playing produces low LED levels.
- Hard playing produces high LED levels.
- LED response is smooth.
- No visible flicker.
- No significant latency.

---

## Phase 3 - Peak / Attack Detection

Goal: Detect individual guitar attacks.

Tasks:

- Calculate short-term envelope.
- Compare against previous envelope.
- Add threshold and hysteresis.
- Add cooldown/refractory period.
- Produce attack strength from 0.0 to 1.0.

Acceptance criteria:

- Individual picked notes generate attacks.
- Sustained notes do not repeatedly trigger attacks.
- Strong strums produce stronger attacks.

---

## Phase 4 - Pitch Detection

Goal: Detect single-note guitar pitch.

Tasks:

- Implement autocorrelation or YIN.
- Start with the standard guitar range.
- Detect E2 through at least E5 if practical.
- Calculate confidence.
- Convert frequency to MIDI note.
- Calculate cents deviation.
- Smooth pitch over time.

Acceptance criteria:

- Open strings are reliably detected.
- Single fretted notes are reliably detected.
- Noise does not continuously create false notes.
- Pitch changes do not cause excessive LED flicker.

Do not attempt chord recognition.

---

## Phase 5 - Pitch Movement

Goal: Detect rising/falling notes.

Tasks:

- Maintain previous stable pitch.
- Calculate pitch delta per unit time.
- Smooth pitch velocity.
- Detect rising/falling trends.
- Trigger directional animations.

Acceptance criteria:

- Slides upward produce a visually rising effect.
- Slides downward produce a visually falling effect.
- Vibrato does not constantly trigger large directional movements.

---

## Phase 6 - Frequency Analysis

Goal: Add richer audio-reactive effects.

Possible implementations:

- FFT
- Goertzel filters
- IIR/FIR band-pass filters

Analyze:

- Low
- Mid
- High

frequency energy.

Acceptance criteria:

- Low-frequency guitar content produces low-band activity.
- Pick/high-frequency content produces high-band activity.
- Effects remain stable while LEDs update.

FFT is optional for the first working version.

---

## Phase 7 - Effect Engine

Goal: Build the final visual system.

Implement:

- VU Meter
- Peak Flash
- Pitch Color
- Pitch Position
- Rising/Falling
- Frequency Bands
- Sustain
- Rainbow
- Breathing
- Static/Manual color

All effects should use the common `AudioFeatures` structure.

Avoid blocking delays.

Use `millis()` or another non-blocking timing approach.

---

## Phase 8 - Mode Control

Add a physical button or rotary encoder.

Possible modes:

```text
0 - VU
1 - Peak
2 - Pitch
3 - Pitch Direction
4 - Frequency Bands
5 - Hybrid
6 - Rainbow
7 - Custom
```

A long press may control brightness.

A short press changes effect.

---

## Phase 9 - Power / Guitar Integration

Integrate the electronics into the guitar.

Tasks:

- Secure ESP32.
- Secure analog audio circuit.
- Secure LED wiring.
- Add appropriate power regulation.
- Add decoupling.
- Add connectors for serviceability.
- Separate analog and high-current LED wiring.
- Test with strings, pickups, pots, and output jack fully installed.

If using a battery:

```text
Battery
  |
Protection / Charger
  |
5V Regulation
  |
+---- ESP32
+---- WS2812
```

The exact power architecture must be selected based on the battery voltage, LED current, and desired operating time.

---

## Phase 10 - Optimization

Optimize only after the complete system works.

Measure:

- ADC sampling stability
- DSP CPU usage
- LED update time
- RAM usage
- Latency
- Pitch detection accuracy
- Noise floor
- Battery consumption

Do not prematurely add a high-end external ADC.

---

# Important Design Constraints

## No microphone in the primary design

The primary system should analyze the guitar's electrical output.

An INMP441 microphone may be added later as an optional ambient/audio mode, but it is not required for the main guitar-reactive system.

## No chord recognition

Do not spend development time on chord recognition.

Single-note pitch detection is sufficient.

## Do not block the main loop

Avoid:

```cpp
delay(...)
```

for effect timing.

Use non-blocking timing.

## LED updates must not destroy audio timing

Audio sampling must remain deterministic enough for DSP.

Prefer buffered sampling and separate processing from LED rendering.

## Keep DSP and LED rendering separate

The audio layer should produce features.

The effect layer should consume features.

---

# Initial Pin Plan

For a typical ESP32-WROOM-32 development board, a possible starting configuration is:

```text
GPIO34  -> Guitar ADC input
GPIO18  -> WS2812 data
GPIO25  -> Mode button
```

GPIO34 is input-only and is suitable for ADC input.

Do not blindly assume every ESP32 development board has identical pin labeling. Verify the exact board before wiring.

Avoid using ESP32 boot-strapping pins unnecessarily.

---

# Wi-Fi Control Portal (current firmware)

The ESP32 runs its own Wi-Fi access point and serves a control page. No router or app is needed.

```text
Wi-Fi network : GuitarRGB
Password      : guitarled
URL           : http://192.168.4.1/   (or http://guitar.local/ on macOS/iOS)
```

The page shows a live preview of the 16 LEDs and controls power, brightness, effect, speed, trail length, static color, a **note → color** table (12 pitch classes, C to B, any octave), and the **pitch → position** range (lowest/highest note on the strip, plus a reverse switch for how the strip is mounted). Mappings are per single note, not per chord, because chord recognition is out of scope.

Effects (the number is what saved settings store; new effects are only ever appended):

| # | Group | Effect | Behaviour |
|---|---|---|---|
| 0 | Ambient | Static color | One color on every LED |
| 1 | Ambient | Rainbow | Scrolling rainbow. Speed |
| 2 | Ambient | Breathing | Static color fading in and out. Speed |
| 9 | Ambient | Twinkle | Random LEDs light in the static color (blended toward white) and fade. Speed = stars per second (~1-21), Trail = glow time |
| 10 | Ambient | Scanner | One smooth dot in the static color bounces end to end with a fading tail. Speed = sweep rate, Trail = tail |
| 11 | Ambient | Ocean | FastLED ocean palette drifting along the strip with a slow shimmer. Speed = drift |
| 12 | Ambient | Lava | FastLED lava palette drifting along the strip with a slow shimmer. Speed = drift |
| 3 | Whole strip | VU meter | Lit LED count follows level (readme Effect 1) |
| 4 | Whole strip | Pitch color | All LEDs take the current note's color (readme Effect 3) |
| 15 | Whole strip | Center VU | Level bar grows from the middle to both ends, soft edge, green centre to red ends |
| 13 | Whole strip | Peak flash | VU meter plus a mostly white flash (slightly tinted by the note) on each attack, strength max(0.35, attack strength), fading in ~120 ms (readme Effect 2) |
| 14 | Whole strip | Sustain | Whole strip in the note color: full on a pick, follows the level while the note rings, then fades. Trail = fade time (~0.1-2 s) (readme Effect 7) |
| 16 | Whole strip | Fire | Fire2012-style flames rising from LED 0. Sparking grows with level, attacks add heat at the base, embers when silent. Trail = flame height |
| 5 | Moving | Pitch position | Dot at the note's place between lowest and highest note, with a trail (readme Effect 4) |
| 6 | Moving | Pitch direction | Bands travel up while pitch rises, down while it falls, still on a steady note (readme Effect 5) |
| 7 | Moving | Note flow | Each note's color enters at one end and scrolls along the strip |
| 8 | Moving | Ripple | Each pick sends pulses outward from the note's position |
| 17 | Moving | Sparkle | Each attack lights 2-10 random LEDs (more for harder picks) in the note color blended toward white; a ringing note adds occasional dimmer sparks. Trail = fade |
| 18 | Moving | Comet | Each attack launches a comet from LED 0 in the note color, up to 4 at once. Speed x pick strength = comet speed, Trail = tail |
| 19 | Moving | Step chase | Each attack moves a 4-LED block one block along the strip with a ~60 ms glide, wrapping around. Brightness follows the level, Trail = fade (readme "rhythm response") |

Notes:

- Effects that color by note use the **static color** while no pitch is known. Peak flash uses white. Sustain keeps the last note color through its fade.
- Everything with a direction respects **Reverse**.
- The page dims the Speed, Trail and Static color rows when the selected effect does not use them.
- Pitch direction caps band speed so a note jump does not alias into backwards motion, and ignores pitch changes under 1.5 semitones/s so vibrato does not read as movement.
- Fire's cooling and sparking ranges were tuned for 16 LEDs by simulating the heat model: Fire2012's defaults (made for ~30 LEDs) flood a short strip with heat at high level.

## Presets

A preset is a one-tap look. It sets only the **effect, speed, trail, static color and note colors**. It never changes power, brightness, pitch range, Reverse, the audio source, noise gate / full scale / release / attack threshold, or the simulated-guitar settings.

| # | Preset | Effect | Speed | Trail | Colors |
|---|---|---|---|---|---|
| 0 | Classic VU | VU meter | 50 | 50 | Rainbow |
| 1 | Rainbow notes | Pitch color | 50 | 50 | Rainbow |
| 2 | Melody trail | Pitch position | 50 | 85 | Rainbow |
| 3 | Slide | Pitch direction | 60 | 40 | Neon |
| 4 | Blues | Note flow | 30 | 80 | Ice |
| 5 | Stage | Ripple | 75 | 30 | Neon |
| 6 | Metal | Ripple | 95 | 15 | Fire |
| 7 | Chill | Breathing | 15 | 50 | static #ff8c1a |
| 8 | Party | Rainbow | 80 | 50 | - |
| 9 | Campfire | Fire | 50 | 70 | - |
| 10 | Night ocean | Ocean | 20 | 50 | - |
| 11 | Strobe | Peak flash | 50 | 50 | Neon |
| 12 | Fireworks | Sparkle | 50 | 40 | Neon |
| 13 | Rhythm | Step chase | 50 | 50 | Neon |
| 14 | Starry night | Twinkle | 30 | 80 | static #b0c8ff |

Note color sets (C to B):

```text
Rainbow  ff0000 ff7f00 ffff00 7fff00 00ff00 00ff7f 00ffff 007fff 0000ff 7f00ff ff00ff ff007f
Fire     ff0000 ff2200 ff4800 ff6a00 ff8c00 ffb200 ffd400 ffb200 ff8c00 ff6a00 ff4800 ff2200
Ice      00ffff 00d4ff 00aaff 007fff 0055ff 002aff 0000ff 002aff 0055ff 007fff 00aaff 00d4ff
Neon     ff00ff 00ffff aaff00 ff0080 00ff80 ffff00 8000ff 00aaff ff5500 ff00aa 00ff00 0055ff
```

The preset table lives in firmware (`src/Presets.cpp`, name + partial settings JSON) and is applied through the same settings merge and 2-second deferred save as the web page, so the Phase 8 mode button can cycle presets without a phone. On the page, the **Presets** card sits right below the live preview: tapping a preset applies it, refreshes every control and shows "Preset: name".

The **Audio** panel selects the source (Simulated guitar or Guitar input on GPIO34), shows a live level meter with an attack indicator and a pitch readout (note, cents, confidence), and sets the noise gate, full scale, release time, attack threshold and pitch confidence (see Audio Analysis below).

The **Simulated guitar** panel drives the simulated source: **Auto-play** picks notes with varied strength, slides and rests; with auto-play off, choosing a note picks it at the set **pick strength**, and **Pluck** picks again.

Settings apply to the LEDs immediately and are saved to flash (NVS) after 2 seconds without further changes, so they survive power cycles.

API (JSON):

```text
GET  /api/config   current settings
POST /api/config   partial or full update, applied live
POST /api/reset    restore defaults
GET  /api/leds     {"leds": [...], "level", "db", "rise", "attacks", "note", "cents", "conf"}: LED colors (LED 0 first), audio level and pitch
POST /api/pluck    pick the simulated guitar once
GET  /api/adc      guitar input stats + latest 512 samples
POST /api/tone?on=1|0   DAC test tone on GPIO26
GET  /api/presets  {"presets": ["Classic VU", ...]}
POST /api/preset?i=N    apply preset N, returns the full settings JSON
```

The **Guitar input** panel shows a live oscilloscope of GPIO34, DC level, peak-to-peak, RMS, measured sample rate, and a status: OK, No signal, Bias off, Clipping, or Test tone.

USB serial commands (115200 baud, one per line), for configuring and testing without Wi-Fi:

```text
{json}   apply settings, same format as POST /api/config (saved immediately)
cfg      print current settings
presets  list the presets
preset N apply preset N (deferred save, like the web page)
leds     print the LED buffer as hex, LED 0 first
pluck    pick the simulated guitar once
feat     audio features: level, dB, onset, attacks, pitch (note, cents, confidence), simulator truth, DSP CPU %
adc      guitar input statistics
scope    latest 512 raw ADC samples
tone on / tone off   DAC test tone on GPIO26
```

Firmware notes:

- FastLED is pinned to 3.7.8. FastLED 3.10.x does not detect the ESP32 RMT driver on Arduino-ESP32 core 2.0.x and falls back to a blocking bit-bang driver that flickers with Wi-Fi active.
- The `huge_app.csv` partition (3 MB app, no OTA) is used. Revisit when OTA updates are added.
- Guitar input uses the I2S peripheral in built-in ADC mode with DMA, so sample timing is set by hardware. Measured on this board:
  - The I2S clock divider is 8 bits and wraps below ~19.6 kHz (16 kHz requested gives ~89 kHz). I2S runs at 32 kHz and sample pairs are averaged to 16 kHz.
  - Samples arrive swapped in pairs; pair averaging cancels this.
  - Data is not inverted.
  - The true output rate is ~15.94 kHz (32000 × 255/256 ÷ 2). DSP must use the measured `sampleRate`, not the nominal 16 kHz.
  - Changing the I2S ADC channel at runtime hangs the driver. The channel is fixed at boot.

---

# Audio Analysis (Phases 2-4)

```text
AudioInput (I2S ADC on GPIO34, or GuitarSynth)  ->  DC removal
   -> AudioDsp -> AudioAnalyzer:
        EnvelopeDetector -> dB -> 0..1 level          (Phase 2)
        AttackDetector   -> attack events             (Phase 3)
        PitchDetector    -> YIN estimate every 16 ms  (Phase 4)
        PitchTracker     -> steady note, cents, hold  (Phase 4)
   -> AudioFeatures -> EffectEngine
```

The DSP classes live in `lib/dsp/` as plain C++ (no Arduino). `AudioAnalyzer` holds the whole chain, so the ESP32 and the host unit tests run exactly the same code; `src/AudioDsp` is a thin wrapper that adds thread-safe snapshots and CPU timing.

## Phase 2: VU level

- Peak envelope of the DC-free signal: 2 ms attack, configurable release (default 120 ms).
- Converted to dB relative to the clean swing above the 1.65V bias (0.8V), then mapped linearly in dB from the **noise gate** (level 0, default -45 dB) to **full scale** (level 1, default -6 dB). dB mapping makes LED steps feel even from soft to hard playing.

## Phase 3: Attack detection

A guitar note is a stack of harmonics, and re-picking a ringing string often barely changes the overall level. High notes keep their own harmonics in the high frequencies, so "high frequencies went up" is not reliable either. What every attack does have is a part that cannot be predicted from the vibration just before it: the pick click, scrape noise, or a new pitch.

- An adaptive linear predictor (NLMS, 24 taps) learns the current note's harmonic mix and cancels it. Its prediction residual stays small on a ringing note and jumps on an attack.
- Two smooth envelopes are checked every 4 ms: the residual (catches re-picks in any register) and the broadband level (attacks from silence, palm mutes).
- Onset value = how much either rose over the last 12 ms. It is large only on the rising edge, so the detector re-arms quickly for fast picking, while ringing notes, slides and vibrato change far less than 1 dB per 12 ms.
- Fires above the **attack threshold** (default 6 dB) when the level is above the noise gate; re-arms below a third of the threshold; 60 ms refractory period. Attack strength = level at the attack.
- Envelopes, not raw per-hop energy: a 4 ms hop is shorter than one period of low E (12 ms), so raw energy swings several dB with wave phase and re-triggers on a single note.

## Phase 4: Pitch detection

Real notes are stacks of harmonics, and on low strings the 2nd or 3rd harmonic is often louder than the fundamental. A "strongest FFT peak" detector would then report the note an octave or more too high. **YIN** instead finds the shortest lag at which the whole waveform repeats. Each harmonic repeats sooner on its own, but their sum only repeats at the true period, so YIN reports the played note even with a weak or missing fundamental.

`PitchDetector` (YIN), every 16 ms on the latest ~38 ms of signal:

1. difference function over a 384-sample (24 ms) window, lags for 70-1400 Hz (drop D up to the 24th fret on high E)
2. cumulative mean normalization (no false dips at tiny lags)
3. first lag whose normalized difference dips below the threshold, walked down to its minimum
4. parabolic interpolation for sub-sample precision

The normalized difference at that lag is the aperiodicity: 1 - aperiodicity is the confidence. The **pitch confidence** setting (default 85%) is the YIN threshold. YIN only runs while the level is above the noise gate.

`PitchTracker` turns raw estimates into a steady note for the LEDs:

- moves of up to 1 semitone per frame (vibrato, bends, slides) are followed with light smoothing (25 ms)
- a bigger jump (new note, or a one-frame glitch in a pick transient) must agree over 2 frames before it is accepted
- the reported note has 15 cents of hysteresis: it changes only when the pitch is more than 65 cents away, so an out-of-tune note or a wobble across a boundary does not flicker
- when estimates stop (decay, noise), the last note is held for 150 ms
- outputs: valid, continuous MIDI pitch, steady note, cents, frequency, confidence (readme `PitchState`)

Effects color by the steady note. Pitch is confirmed ~30-60 ms after the pick that starts a note, so when a new note is confirmed within 150 ms of an attack, the ripple, comet or flash that attack spawned is recolored to the new note.

Measured:

| | Result |
|---|---|
| Host tests, open strings and chromatic E2-E5 | correct note, within 0.1 cents on clean tones |
| Host tests, weak or missing fundamental | correct note, no octave errors |
| Host tests, stiff strings (inharmonicity 1e-4) | correct note, reads up to ~7 cents sharp right after the pick, settling to ~1 cent: the sharp overtones are strongest early. 2e-4: up to ~13 cents |
| Board, simulated guitar auto-play, steady notes | 305/305 correct, 0 octave errors |
| Board, time to show a new note | median 43 ms, 90% within 67 ms (target < 100 ms) |
| Board, manual notes E2-E6 at soft and hard pick | 20/20 correct |
| Board, audio task CPU (all DSP) | ~19% of one core (YIN unrolled into 4 accumulators, -O2) |

Two notes ringing at once (a chord, or a previous string still ringing) are out of scope: the tracker reports one of the played notes or holds the last one, and never invents a third.

## Simulated guitar (mock source)

`GuitarSynth` replaces the ADC samples when Source = Simulated guitar, so the whole chain runs exactly as it will with the real guitar. It produces what GPIO34 would see through the MCP6022 buffer: a plucked string (`lib/dsp/PluckSynth`, 16 harmonics with faster decay for higher ones, pick noise, pick restarts the string from rest) on the 1.65V bias, plus ADC noise and 50 Hz hum. Pitch is detected from these samples like from the guitar; the note the simulator is playing is only reported alongside (`truth` in the serial `feat` output) to check detection.

## Host unit tests

```text
pio test -e native
```

`test/test_dsp` plays scripted guitar parts through `PluckSynth` with ADC noise and hum, and checks the same DSP code the firmware runs:

- silence: level 0, no attacks
- soft pick gives a low level, hard pick a high level; harder pick gives a stronger attack
- 8 different notes and strengths: exactly one attack each, within 12 ms
- re-picking a ringing string, fast picking every 100 ms (E3, E4, E5)
- a second string picked while the first rings
- sustained note, slide, vibrato: no extra attacks
- level falls within 0.8 s of muting

`test/test_pitch` runs the same way through `AudioAnalyzer`:

- open strings, chromatic E2-E5, top of the neck up to E6
- weak fundamental (1st harmonic at 10%) and missing fundamental: no octave errors
- stiff strings (inharmonicity 1e-4 and 2e-4)
- notes 40 cents sharp or flat keep their name
- loud noise: no pitch
- latency from silence < 100 ms, note change < 90 ms with no wrong notes in between
- vibrato: no note flicker; slide: notes in order
- two strings ringing: no invented notes
- pitch drops after muting

## Phase 2-4 test procedure (with the guitar)

1. Source = Guitar input. With the guitar volume down, the level meter should stay empty and Pitch should show "–". If not, raise the noise gate until it does.
2. Play softly, then hard. Soft playing should light a few LEDs (VU meter effect), hard playing most of them. Adjust full scale so your hardest playing just reaches the top.
3. Pick single notes: the attack dot should flash once per pick. Let a note ring: no extra flashes. If picks are missed, lower the attack threshold; if a ringing note flashes, raise it.
4. Fast picking on one string: one flash per pick.
5. Play each open string and compare the Pitch readout with a tuner app: same note, cents within a few cents (a little sharp right after the pick on wound strings is normal).
6. Play single fretted notes up the neck: the readout follows without flickering. Slow vibrato should not change the note. If notes drop out on soft playing, lower pitch confidence a little; if the readout jumps around on messy notes, raise it.

---

# Phase 1 Analog Circuit

Unity-gain MCP6022 buffer on a single 3.3V supply, biased at 1.65V.

```text
Bias (1.65 V)                           Supply

3.3V --[100k]--+--[100k]-- GND          3.3V --+-- MCP6022 pin 8 (VDD)
               |                               |
             VBIAS                           100nF   (close to pin 8)
               |                               |
            [10uF]                            GND --- MCP6022 pin 4 (VSS)
               |
              GND

Input buffer (op-amp A)

Guitar tip --[100nF]--+--[100k]-- pin 3 (+IN A)
                      |
                   [2.2M]                pin 2 (-IN A) ---- pin 1 (OUT A)
                      |
                    VBIAS

pin 1 (OUT A) --[1k]--+-- GPIO34
                      |
                   [22nF]
                      |
                     GND

Unused op-amp B:  pin 5 (+IN B) -- VBIAS,   pin 6 (-IN B) -- pin 7 (OUT B)
Guitar sleeve:    GND (common with ESP32 GND)
```

MCP6022-I/P pinout (DIP-8, notch at the top, pin 1 top-left):

```text
OUT A  1 +--U--+ 8  VDD
-IN A  2 |     | 7  OUT B
+IN A  3 |     | 6  -IN B
VSS    4 +-----+ 5  +IN B
```

Pin-by-pin wiring. Both op-amps must be wired; do not leave op-amp B floating.

| Pin | Name | Connect to |
|---|---|---|
| 1 | OUT A | Pin 2 directly, and through the **1kΩ** resistor to GPIO34 (22nF from GPIO34 to GND). Never wire pin 1 straight to GPIO34 |
| 2 | -IN A | Pin 1 (voltage follower) |
| 3 | +IN A | Guitar signal after the 100nF coupling capacitor and 100kΩ series resistor; 2.2MΩ from this node to VBIAS |
| 4 | VSS | GND |
| 5 | +IN B | VBIAS (1.65V) |
| 6 | -IN B | Pin 7 |
| 7 | OUT B | Pin 6 only. No connection to the ESP32 or the signal path |
| 8 | VDD | 3.3V, with 100nF to GND right at the pin |

Op-amp B is not part of the audio path. It is parked as a follower at VBIAS so its inputs and output sit in a defined, stable state.

| Part | Role |
|---|---|
| 100nF input capacitor | AC coupling: blocks DC, passes audio (corner ~0.7 Hz with 2.2MΩ) |
| 2.2MΩ to VBIAS | Holds the input at 1.65V; the load the pickups see (with the amp's 1MΩ in parallel, ~690kΩ total) |
| 100kΩ series | Limits current into the op-amp's input protection diodes if a very hot signal exceeds the rails |
| 100kΩ + 100kΩ + 10µF | 1.65V bias, filtered (corner ~0.3 Hz) so Wi-Fi noise on the 3.3V rail stays out of the signal |
| MCP6022 A | Unity-gain buffer: high impedance in, low impedance out to the ADC |
| 1kΩ + 22nF | Anti-aliasing low-pass at ~7 kHz and a charge reservoir for the ADC. The 1kΩ also keeps the 22nF from loading the op-amp output directly (prevents ringing) |
| 100nF at pin 8 | Op-amp supply decoupling |
| MCP6022 B | Unused; parked as a follower at VBIAS so it cannot oscillate |

Signal tap for testing: guitar into a Y splitter, one branch to the amp, the other to a 6.35 mm jack on the breadboard.

Headroom: the ESP32 ADC is accurate from about 0.15V to 2.45V at 11 dB attenuation, so the 1.65V bias allows about 0.8V of clean swing upward. Passive pickups usually stay inside that at unity gain. Add gain only if the measured signal is small (see Phase 1 test procedure): replace the follower link with a non-inverting stage whose gain resistor returns to VBIAS through a capacitor, so DC gain stays 1.

If the amp hums when the circuit is connected, it is likely a ground loop through USB. Run the ESP32 from a power bank or a laptop on battery.

Do not connect a potentially negative guitar signal directly to the ESP32.

## Phase 1 Test Procedure

Open the control page, expand **Guitar input (GPIO34)**.

1. **Loopback (no circuit).** Nothing on GPIO34. Jumper GPIO26 to GPIO34, switch **Test tone** on. Expect a clean sine, DC about 1.1V, peak-to-peak about 0.8V, about 260 crossings/s (the DAC tone generator steps in ~130 Hz, ±5% per chip), clipped 0. Switch the tone off and **remove the jumper**.
2. **Circuit, no guitar.** Build the circuit, measure VBIAS with a multimeter (about 1.65V), connect the 1kΩ output to GPIO34. Expect DC about 1.65V, peak-to-peak under 0.02V, status "No signal".
3. **Guitar.** Connect through the Y splitter. Play softly, then strum hard. Expect a visible waveform, status "OK", clipped 0, DC staying near 1.65V.
4. **Tone check.** Listen through the amp with the ESP32 branch connected and disconnected. There should be no audible difference.

Readings are also available over USB serial with the `adc` and `scope` commands.

---

# Testing Strategy

Each phase must have an independent test.

Examples:

### LED test

```text
Rainbow -> Red -> Green -> Blue -> Off
```

### ADC test

Display:

```text
min
max
average
RMS
```

### VU test

Print:

```text
level=0.72
```

and display corresponding LED count.

### Pitch test

Print:

```text
frequency=110.3 Hz
note=A2
confidence=0.91
cents=+4
```

### Attack test

Print:

```text
ATTACK strength=0.83
```

This makes debugging much easier than debugging the full system at once.

---

# Performance Targets

Initial targets:

- Audio sample rate: 8-16 kHz
- 16 WS2812 LEDs
- Pitch detection latency target: <100 ms
- LED animation target: 30-60 FPS
- Stable operation without audio dropouts
- No audible impact on the guitar's normal output

These are targets, not absolute requirements. Optimize based on measured behavior.

---

# Future Features

Possible future additions:

- Bluetooth configuration
- Wi-Fi configuration page
- Mobile app
- OTA firmware update
- Saving your own presets (15 built-in presets exist, see Presets)
- User-defined color palettes
- MIDI output
- Tuner mode
- Metronome synchronization
- Battery level display
- Accelerometer/IMU for motion-reactive effects
- Ambient microphone mode
- Multiple pickup/input modes

---

# Recommended Development Order

Do not implement everything at once.

Follow this order:

```text
ESP32
  ↓
16 LEDs
  ↓
Guitar ADC
  ↓
VU Meter
  ↓
Attack Detection
  ↓
Pitch Detection
  ↓
Pitch Direction
  ↓
Frequency Analysis
  ↓
Effect Engine
  ↓
Mode Button
  ↓
Final Guitar Integration
  ↓
Optimization
```

The project should always have a working state after each phase.

---

# Claude Code Instructions

When working on this project:

1. Do not rewrite working hardware code unnecessarily.
2. Implement one phase at a time.
3. Before moving to the next phase, provide a short test procedure.
4. Keep audio acquisition, DSP, and LED rendering modular.
5. Avoid blocking code.
6. Do not introduce external hardware unless it solves a demonstrated problem.
7. Prefer ESP32 internal ADC for the first implementation.
8. Do not introduce ADS1115 for audio.
9. Do not introduce MCP3201 unless there is a demonstrated requirement.
10. Do not introduce PCM1808 or another external audio ADC unless ESP32 ADC performance has been measured and found insufficient.
11. Do not implement chord recognition.
12. Treat single-note pitch detection as the pitch-analysis requirement.
13. Preserve a clean separation between raw audio acquisition and visualization.
14. Make thresholds, sample rate, gain, LED brightness, colors, and effect parameters configurable.
15. Document all hardware assumptions.
16. If a hardware change is required, explain why before changing the architecture.

---

# Definition of Done

The project is considered successful when:

- The guitar plays normally through its amplifier.
- The ESP32 receives a safe version of the guitar signal.
- The 16 WS2812 LEDs operate reliably.
- Guitar volume changes affect the LEDs.
- Pick attacks produce visible reactions.
- Single notes can be detected.
- Rising and falling pitch can trigger directional effects.
- Frequency content can drive additional effects.
- The system remains responsive while audio analysis and LED animation run simultaneously.
- No noticeable degradation of the guitar signal is introduced.
- The complete system can operate reliably inside the DIY guitar.
