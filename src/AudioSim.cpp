#include "AudioSim.h"
#include <math.h>

static float    curMidi    = 52.0f;   // E3
static float    targetMidi = 52.0f;
static float    prevMidi   = 52.0f;
static float    velocity   = 0.0f;    // smoothed, semitones/s
static float    env        = 0.0f;    // 0..1 amplitude envelope
static uint32_t lastNow    = 0;
static uint32_t nextNoteAt = 0;
static int16_t  lastManual = -2;      // forces an attack on the first manual note
static volatile bool pluckPending = false;

void audioSimPluck() { pluckPending = true; }

void audioSimUpdate(const Settings& s, uint32_t now, AudioFeatures& f) {
  float dt = lastNow ? (now - lastNow) / 1000.0f : 0.016f;
  if (dt <= 0.0f || dt > 0.25f) dt = 0.016f;
  lastNow = now;

  bool  attack   = false;
  float strength = 1.0f;

  if (s.testAuto) {
    if ((int32_t)(now - nextNoteAt) >= 0) {
      // Random walk inside the mapped range: mostly small steps, some leaps.
      float step = (float)random(-5, 6);
      targetMidi = constrain(roundf(targetMidi + step), (float)s.pitchLow, (float)s.pitchHigh);
      attack     = true;
      strength   = random(55, 101) / 100.0f;
      nextNoteAt = now + random(300, 900);
    }
  } else if (s.testNote != lastManual) {
    lastManual = s.testNote;
    if (s.testNote >= 0) {
      targetMidi = s.testNote;
      attack     = true;
    }
  }

  if (pluckPending) {
    pluckPending = false;
    attack   = true;
    strength = 1.0f;
  }

  // Glide toward the target so note changes look like short slides.
  curMidi += (targetMidi - curMidi) * fminf(1.0f, dt * 8.0f);

  float raw = (curMidi - prevMidi) / dt;
  prevMidi  = curMidi;
  velocity += (raw - velocity) * fminf(1.0f, dt * 10.0f);

  if (attack) env = fmaxf(env, strength);
  env *= expf(-dt * 2.5f);   // ~0.4 s decay

  if (s.testAuto) {
    f.level      = env;
    f.pitchValid = env > 0.03f;
  } else {
    f.level      = fmaxf(s.testLevel, env);   // hold manual level; pluck adds a transient
    f.pitchValid = s.testNote >= 0;
  }
  f.attackDetected = attack;
  f.attackStrength = attack ? strength : 0.0f;
  f.midiNote       = curMidi;
  f.pitchVelocity  = velocity;
}
