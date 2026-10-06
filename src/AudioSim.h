/*
 * AudioSim.h — stand-in for the audio/DSP layer until guitar input exists.
 *
 * Auto mode imitates a player: picked notes that decay, moving through the
 * configured pitch range with short slides between notes.
 * Manual mode holds the note and level chosen in the web UI; "Pluck" adds
 * an attack on top.
 */
#pragma once
#include "Config.h"
#include "AudioFeatures.h"

void audioSimUpdate(const Settings& s, uint32_t now, AudioFeatures& out);
void audioSimPluck();   // trigger one attack (web UI button / serial command)
