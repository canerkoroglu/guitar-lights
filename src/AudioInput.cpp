#include "AudioInput.h"
#include <driver/i2s.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <math.h>
#include "Config.h"
#include "GuitarSynth.h"
#include "AudioDsp.h"

/*
 * ESP32 I2S built-in ADC notes (Arduino-ESP32 2.0.14 / IDF 4.4), measured
 * on this board:
 *
 * - The I2S master clock divider is 8 bits. In ADC mode the driver needs
 *   160 MHz / (rate * 32), which only fits for rates >= ~19.6 kHz; at
 *   16 kHz it silently wraps (312 -> 56) and the ADC runs at ~89 kHz.
 *   So I2S runs at 32 kHz and pairs of samples are averaged to 16 kHz.
 *   Averaging also lowers ADC noise.
 * - Samples arrive swapped in pairs (an edge reads H H L H L L).
 *   Averaging each pair makes the order irrelevant.
 * - Data is not inverted (pull-up reads 4095, pull-down reads 0).
 * - Actual I2S rate is 31875 Hz (32000 * 255/256), so the output is
 *   ~15.94 kHz. Stats::sampleRate reports the measured value; DSP should
 *   use it rather than the nominal SAMPLE_RATE.
 */

namespace {

constexpr i2s_port_t     I2S_PORT  = I2S_NUM_0;
constexpr adc1_channel_t CHANNEL   = ADC1_CHANNEL_6;   // GPIO34
constexpr uint32_t       I2S_RATE  = AudioInput::SAMPLE_RATE * 2;
constexpr size_t         DMA_LEN   = 512;              // raw samples per DMA buffer (16 ms)
constexpr uint32_t       WINDOW_US = 250000;           // statistics window
constexpr float          DC_POLE   = 0.995f;           // DC blocker, ~13 Hz corner at 16 kHz
constexpr float          ZC_HYST   = 12.0f;            // raw LSB hysteresis for zero crossings

esp_adc_cal_characteristics_t cal;
portMUX_TYPE      mux = portMUX_INITIALIZER_UNLOCKED;
AudioInput::Stats published = {};
uint16_t          ring[AudioInput::SCOPE_LEN] = {};
size_t            ringPos = 0;
uint16_t          rawLo = 0, rawHi = 4095;

float rawToV(int raw) {
  return esp_adc_cal_raw_to_voltage((uint32_t)constrain(raw, 0, 4095), &cal) / 1000.0f;
}

void audioTask(void*) {
  static uint16_t dma[DMA_LEN];
  static uint16_t block[DMA_LEN / 2];
  static float    ac[DMA_LEN / 2];

  float    dcX = 0.0f, dcY = 0.0f;
  bool     primed = false, above = false;
  uint32_t n = 0, clipped = 0, crossings = 0;
  uint16_t mn = 4095, mx = 0;
  double   sum = 0.0, sumSq = 0.0;
  uint32_t winStart = micros();

  for (;;) {
    size_t bytes = 0;
    if (i2s_read(I2S_PORT, dma, sizeof(dma), &bytes, portMAX_DELAY) != ESP_OK) continue;

    // 32 kHz -> 16 kHz: average each pair. Top 4 bits carry the channel number.
    const size_t count = bytes / (2 * sizeof(uint16_t));
    for (size_t i = 0; i < count; i++) {
      block[i] = ((dma[2 * i] & 0x0FFF) + (dma[2 * i + 1] & 0x0FFF) + 1) / 2;
    }

    const Settings& settings  = ConfigStore::get();
    const bool      simulated = settings.audioSource == SOURCE_SIMULATED;
    if (simulated) GuitarSynth::render(block, count, settings);

    for (size_t i = 0; i < count; i++) {
      const uint16_t raw = block[i];

      // DC removal: one-pole high-pass, y[n] = x[n] - x[n-1] + R * y[n-1]
      const float x = raw;
      if (!primed) { dcX = x; primed = true; }
      const float y = x - dcX + DC_POLE * dcY;
      dcX = x;
      dcY = y;
      ac[i] = y;

      if (raw < mn) mn = raw;
      if (raw > mx) mx = raw;
      if (raw < rawLo || raw > rawHi) clipped++;
      sum   += raw;
      sumSq += (double)y * y;

      if (!above && y > ZC_HYST)      { above = true; crossings++; }
      else if (above && y < -ZC_HYST) { above = false; }
    }
    n += count;

    AudioDsp::process(ac, count, settings, simulated);

    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < count; i++) {
      ring[ringPos] = block[i];
      ringPos = (ringPos + 1) % AudioInput::SCOPE_LEN;
    }
    portEXIT_CRITICAL(&mux);

    const uint32_t now = micros();
    if (now - winStart >= WINDOW_US && n > 0) {
      const float secs = (now - winStart) / 1e6f;
      const int   mean = (int)lround(sum / n);
      // Local volts-per-LSB around the operating point, to convert RMS.
      const float vPerLsb = (rawToV(mean + 100) - rawToV(mean - 100)) / 200.0f;

      AudioInput::Stats s;
      s.running     = true;
      s.sampleRate  = n / secs;
      s.dcV         = rawToV(mean);
      s.minV        = rawToV(mn);
      s.maxV        = rawToV(mx);
      s.p2pV        = s.maxV - s.minV;
      s.rmsV        = sqrtf((float)(sumSq / n)) * vPerLsb;
      s.clipped     = clipped;
      s.zeroCrossHz = crossings / secs;
      s.rawLo       = rawLo;
      s.rawHi       = rawHi;

      portENTER_CRITICAL(&mux);
      published = s;
      portEXIT_CRITICAL(&mux);

      n = clipped = crossings = 0;
      mn = 4095;
      mx = 0;
      sum = sumSq = 0.0;
      winStart = now;
    }
  }
}

}  // namespace

bool AudioInput::begin() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  cfg.sample_rate          = I2S_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = DMA_LEN;
  cfg.use_apll             = false;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
  // The I2S pattern table hard-codes 11 dB attenuation (~0..3.1 V); this
  // call only routes the pin to the ADC.
  adc1_config_channel_atten(CHANNEL, ADC_ATTEN_DB_11);
  if (i2s_set_adc_mode(ADC_UNIT_1, CHANNEL) != ESP_OK) return false;
  if (i2s_adc_enable(I2S_PORT) != ESP_OK) return false;

  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &cal);
  // Raw limits of the range Espressif specifies as accurate at 11 dB.
  while (rawLo < 4095 && rawToV(rawLo) < 0.150f) rawLo++;
  while (rawHi > 0 && rawToV(rawHi) > 2.450f) rawHi--;

  // Bias point and scale for the simulated guitar and the VU full scale.
  int biasRaw = rawLo;
  while (biasRaw < rawHi && rawToV(biasRaw) < 1.650f) biasRaw++;
  const float lsbPerVolt = (rawHi - rawLo) / (2.450f - 0.150f);
  GuitarSynth::begin(ACTUAL_RATE, biasRaw, lsbPerVolt);
  AudioDsp::begin(ACTUAL_RATE, (float)(rawHi - biasRaw));   // clean swing above bias

  return xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1) == pdPASS;
}

AudioInput::Stats AudioInput::stats() {
  portENTER_CRITICAL(&mux);
  Stats s = published;
  portEXIT_CRITICAL(&mux);
  return s;
}

size_t AudioInput::scope(uint16_t* out, size_t n) {
  if (n > SCOPE_LEN) n = SCOPE_LEN;
  portENTER_CRITICAL(&mux);
  const size_t start = (ringPos + SCOPE_LEN - n) % SCOPE_LEN;
  for (size_t i = 0; i < n; i++) out[i] = ring[(start + i) % SCOPE_LEN];
  portEXIT_CRITICAL(&mux);
  return n;
}
