/*
 * WebPortal.h — HTTP server for the LED control UI.
 *
 *   GET  /            -> control page
 *   GET  /api/config  -> current Settings as JSON
 *   POST /api/config  -> merge JSON into Settings, apply live (partial bodies
 *                        allowed; the page sends only what changed)
 *                        (saved to NVS after 2 s of no further changes)
 *   GET  /api/effects -> {"effects":[{"mode","group","name","desc","uses"}, ...]}
 *                        in menu order; the page builds its effect list from it
 *   GET  /api/presets -> {"presets":["Classic VU", ...]}
 *   POST /api/preset?i=N -> apply preset N, return the full settings JSON
 *   POST /api/reset   -> restore defaults, save, return them as JSON
 *   GET  /api/leds    -> {"leds":["#rrggbb",...], "level", "db", "rise", "attacks",
 *                         "note" (-1 if none), "cents", "conf"}
 *                        + "tuner", "bat":{"fitted","state","v","pct","bootOnly"}
 *   POST /api/tuner?on=1|0 -> tuner on the LEDs (live state, not saved)
 *   POST /api/battery/show -> battery gauge on the LEDs for 2.5 s
 *   POST /api/pluck   -> pick the simulated guitar once
 *   GET  /api/adc     -> guitar input stats + latest 512 raw samples
 *   POST /api/calibrate -> start a 3 s quiet-input noise measurement
 *   GET  /api/calibrate -> progress/result and suggested noise-gate setting
 *   POST /api/tone?on=1|0 -> DAC test tone on GPIO26 (loopback test)
 */
#pragma once
#include <FastLED.h>

void webPortalBegin(CRGB* leds, uint16_t numLeds);
void webPortalLoop();   // call every loop(); non-blocking
bool webPortalDebugActive();
void webPortalRenderDebug();
