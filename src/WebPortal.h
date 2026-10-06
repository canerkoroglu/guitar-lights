/*
 * WebPortal.h — HTTP server for the LED control UI.
 *
 *   GET  /            -> control page
 *   GET  /api/config  -> current Settings as JSON
 *   POST /api/config  -> merge JSON into Settings, apply live
 *                        (saved to NVS after 2 s of no further changes)
 *   GET  /api/presets -> {"presets":["Classic VU", ...]}
 *   POST /api/preset?i=N -> apply preset N, return the full settings JSON
 *   POST /api/reset   -> restore defaults, save, return them as JSON
 *   GET  /api/leds    -> {"leds":["#rrggbb",...], "level", "db", "rise", "attacks"}
 *   POST /api/pluck   -> pick the simulated guitar once
 *   GET  /api/adc     -> guitar input stats + latest 512 raw samples
 *   POST /api/tone?on=1|0 -> DAC test tone on GPIO26 (loopback test)
 */
#pragma once
#include <FastLED.h>

void webPortalBegin(const CRGB* leds, uint16_t numLeds);
void webPortalLoop();   // call every loop(); non-blocking
