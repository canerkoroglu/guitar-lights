/*
 * EffectCatalog.h — what the web page shows for each effect: name, group,
 * description, and which of Speed / Trail / Static color it uses.
 *
 * Served as GET /api/effects, so the page builds its effect menu from
 * this one list. Adding an effect = a new EffectMode (Config.h), a case
 * in EffectEngine::render(), and one entry here.
 */
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace EffectCatalog {
  struct Info {
    EffectMode  mode;
    const char* group;   // menu section
    const char* name;
    const char* desc;
    const char* uses;    // letters: s = Speed, t = Trail, c = Static color
  };

  size_t      count();
  const Info& at(size_t i);   // in menu order (grouped), not mode order
  bool        selfCheck();    // every mode listed exactly once
}
