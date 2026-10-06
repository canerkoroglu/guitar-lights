/*
 * WebPortal.h — HTTP server for the LED control UI.
 *
 *   GET  /            -> control page
 *   GET  /api/config  -> current Settings as JSON
 *   POST /api/config  -> merge JSON into Settings, apply live
 *                        (saved to NVS after 2 s of no further changes)
 *   POST /api/reset   -> restore defaults, save, return them as JSON
 *   GET  /api/leds    -> current LED colors, ["#rrggbb", ...], LED 0 first
 *   POST /api/pluck   -> trigger one simulated pick attack
 */
#pragma once
#include <FastLED.h>

void webPortalBegin(const CRGB* leds, uint16_t numLeds);
void webPortalLoop();   // call every loop(); non-blocking
