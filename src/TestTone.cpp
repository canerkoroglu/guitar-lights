#include "TestTone.h"
#include <driver/dac.h>

static bool s_on = false;

void TestTone::set(bool on) {
  if (on == s_on) return;
  if (on) {
    dac_cw_config_t cw = {};
    cw.en_ch  = DAC_CHANNEL_2;    // GPIO26
    cw.scale  = DAC_CW_SCALE_4;   // about 0.8 V peak-to-peak
    cw.phase  = DAC_CW_PHASE_0;
    cw.freq   = 260;              // the generator steps in ~130 Hz increments
    cw.offset = -40;              // centred near 1.1 V: off-centre, so an inverted reading is obvious
    dac_cw_generator_config(&cw);
    dac_cw_generator_enable();
    dac_output_enable(DAC_CHANNEL_2);
  } else {
    dac_cw_generator_disable();
    dac_output_disable(DAC_CHANNEL_2);
  }
  s_on = on;
}

bool TestTone::active() { return s_on; }
