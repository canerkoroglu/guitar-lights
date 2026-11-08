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
constexpr adc1_channel_t BAT_CHAN  = ADC1_CHANNEL_7;   // GPIO35
constexpr int            DMA_COUNT = 4;                // DMA buffers
constexpr uint32_t       I2S_RATE  = AudioInput::SAMPLE_RATE * 2;
constexpr size_t         DMA_LEN   = 512;              // raw samples per DMA buffer (16 ms)
constexpr uint32_t       WINDOW_US = 250000;           // statistics window
constexpr float          DC_POLE   = 0.995f;           // DC blocker, ~13 Hz corner at 16 kHz
constexpr float          ZC_HYST   = 12.0f;            // raw LSB hysteresis for zero crossings
constexpr float          BIAS_MIN_V = 1.40f;            // expected analog-stage DC bias is 1.65 V
constexpr float          BIAS_MAX_V = 1.90f;
constexpr uint32_t       CALIBRATION_SAMPLES = (uint32_t)(AudioInput::ACTUAL_RATE * 3.0f);
static_assert(DMA_LEN / 2 <= GuitarSynth::MAX_BLOCK, "GuitarSynth must fill a whole block");

esp_adc_cal_characteristics_t cal;
portMUX_TYPE      mux = portMUX_INITIALIZER_UNLOCKED;
AudioInput::Stats published = {};
uint16_t          ring[AudioInput::SCOPE_LEN] = {};
size_t            ringPos = 0;
uint16_t          rawLo = 0, rawHi = 4095;

volatile bool          batteryRequested = false;
volatile uint32_t      batteryReadAt    = 0;   // millis() of the last runtime read
volatile uint32_t      blocks           = 0;   // completed blocks, for the stall watchdog
volatile int           enableState      = 0;   // audio task start: 0 pending, 1 ok, -1 failed
AudioInput::BatteryPin batteryPublished = {0, 0};
AudioInput::Calibration calibrationPublished = {false, false, 0, -120.0f, -35};
volatile bool calibrationRequested = false;
float fullScaleLsb = 900.0f;

float rawToV(int raw) {
  return esp_adc_cal_raw_to_voltage((uint32_t)constrain(raw, 0, 4095), &cal) / 1000.0f;
}

// Battery pin through the ADC1 RTC controller: 16 samples, the middle 8
// averaged (rejects spikes). Only call while I2S does not own ADC1.
uint32_t readBatteryPinMv() {
  constexpr int N = 16;
  int s[N];
  for (int i = 0; i < N; i++) s[i] = adc1_get_raw(BAT_CHAN);
  for (int i = 1; i < N; i++) {   // insertion sort, N is tiny
    const int v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  int sum = 0;
  for (int i = N / 4; i < N - N / 4; i++) sum += s[i];
  const int raw = sum / (N / 2);
  return raw > 0 ? esp_adc_cal_raw_to_voltage((uint32_t)raw, &cal) : 0;
}

void publishBattery(uint32_t mv) {
  portENTER_CRITICAL(&mux);
  batteryPublished.mv = mv;
  batteryPublished.seq++;
  portEXIT_CRITICAL(&mux);
}

void audioTask(void*) {
  // ADC1's lock is a FreeRTOS mutex: the task that takes it (i2s_adc_enable)
  // must be the one that releases it (i2s_adc_disable, for battery reads).
  // So every enable/disable happens here, in this task, never in setup().
  if (i2s_adc_enable(I2S_PORT) != ESP_OK) {
    enableState = -1;
    vTaskDelete(nullptr);
  }
  enableState = 1;

  static uint16_t dma[DMA_LEN];
  static uint16_t block[DMA_LEN / 2];
  static float    ac[DMA_LEN / 2];

  float    dcX = 0.0f, dcY = 0.0f;
  bool     primed = false, above = false;
  uint32_t n = 0, clipped = 0, crossings = 0;
  uint16_t mn = 4095, mx = 0;
  double   sum = 0.0, sumSq = 0.0;
  uint32_t winStart = micros();
  bool     inputBiasValid = false;
  bool     calibrationActive = false;
  uint32_t calibrationCount = 0;
  double   calibrationSumSq = 0.0;

  int  dropBlocks    = 0;       // blocks to discard after a battery read
  bool restartWindow = false;

  for (;;) {
    size_t bytes = 0;
    if (i2s_read(I2S_PORT, dma, sizeof(dma), &bytes, portMAX_DELAY) != ESP_OK) {
      vTaskDelay(1);   // never spin at this priority: it would starve loop()
      continue;
    }
    blocks++;

    // After a battery read the DMA ring still holds samples taken while
    // ADC1 was away: skip them so the DSP never sees that step.
    if (dropBlocks > 0) {
      dropBlocks--;
      continue;
    }
    if (restartWindow) {   // the gap would distort rate and level statistics
      restartWindow = false;
      n = clipped = crossings = 0;
      mn = 4095;
      mx = 0;
      sum = sumSq = 0.0;
      winStart = micros();
    }

    // 32 kHz -> 16 kHz: average each pair. Top 4 bits carry the channel number.
    const size_t count = bytes / (2 * sizeof(uint16_t));
    for (size_t i = 0; i < count; i++) {
      block[i] = ((dma[2 * i] & 0x0FFF) + (dma[2 * i + 1] & 0x0FFF) + 1) / 2;
    }

    const Settings  settings  = ConfigStore::snapshot();   // this task must not read the live copy
    const bool      simulated = settings.audioSource == SOURCE_SIMULATED;
    if (simulated) GuitarSynth::render(block, count, settings);

    if (calibrationRequested) {
      calibrationRequested = false;
      calibrationActive = true;
      calibrationCount = 0;
      calibrationSumSq = 0.0;
      portENTER_CRITICAL(&mux);
      calibrationPublished = {true, inputBiasValid, 0, -120.0f, settings.gateDb};
      portEXIT_CRITICAL(&mux);
    }

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

    if (calibrationActive) {
      for (size_t i = 0; i < count; i++) calibrationSumSq += (double)ac[i] * ac[i];
      calibrationCount += count;
      if (calibrationCount >= CALIBRATION_SAMPLES) {
        const float rms = sqrtf((float)(calibrationSumSq / calibrationCount));
        const float noiseDb = 20.0f * log10f((rms + 1e-3f) / fullScaleLsb);
        const int suggested = constrain((int)ceilf(noiseDb + 15.0f), -80, -10);
        portENTER_CRITICAL(&mux);
        calibrationPublished = {false, inputBiasValid, 100, noiseDb, (int8_t)suggested};
        portEXIT_CRITICAL(&mux);
        calibrationActive = false;
      } else {
        const uint8_t progress = (uint8_t)(100UL * calibrationCount / CALIBRATION_SAMPLES);
        portENTER_CRITICAL(&mux);
        calibrationPublished.progress = progress;
        calibrationPublished.biasValid = inputBiasValid;
        portEXIT_CRITICAL(&mux);
      }
    }

    // GPIO34 has no internal pull resistor.  If the analog stage has lost
    // its 1.65 V bias, treat the input as silence instead of letting a
    // floating pin manufacture attacks.  The validity flag is refreshed
    // from the calibrated 250 ms statistics window below.
    if (!simulated && !inputBiasValid) {
      for (size_t i = 0; i < count; i++) ac[i] = 0.0f;
    }
    AudioDsp::process(ac, count, settings, simulated);

    // Battery read only in silence: the ~80 ms gap is invisible there.
    if (batteryRequested && AudioDsp::snapshot().db < settings.gateDb) {
      batteryRequested = false;
      batteryReadAt    = millis() | 1;   // never 0
      i2s_adc_disable(I2S_PORT);
      const uint32_t mv = readBatteryPinMv();
      const esp_err_t e = i2s_adc_enable(I2S_PORT);   // restores the I2S ADC pattern for GPIO34
      if (e != ESP_OK) Serial.printf("[audio] i2s_adc_enable after battery read failed: %d\n", (int)e);
      publishBattery(mv);
      dropBlocks    = DMA_COUNT + 1;
      restartWindow = true;
    }

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
      s.biasValid   = s.dcV >= BIAS_MIN_V && s.dcV <= BIAS_MAX_V;

      // Do not require a connected guitar: a correctly built buffer keeps
      // the ADC biased while the guitar is unplugged.  This only rejects a
      // missing/floating analog bias, which is never a playable signal.
      inputBiasValid = simulated || s.biasValid;

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
  // Calibration first: the boot battery reading below needs it.
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &cal);

  // First battery reading, while ADC1 is still free (before I2S takes it).
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BAT_CHAN, ADC_ATTEN_DB_11);
  publishBattery(readBatteryPinMv());

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  cfg.sample_rate          = I2S_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = DMA_COUNT;
  cfg.dma_buf_len          = DMA_LEN;
  cfg.use_apll             = false;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
  // The I2S pattern table hard-codes 11 dB attenuation (~0..3.1 V); this
  // call only routes the pin to the ADC.
  adc1_config_channel_atten(CHANNEL, ADC_ATTEN_DB_11);
  if (i2s_set_adc_mode(ADC_UNIT_1, CHANNEL) != ESP_OK) return false;

  // Raw limits of the range Espressif specifies as accurate at 11 dB.
  while (rawLo < 4095 && rawToV(rawLo) < 0.150f) rawLo++;
  while (rawHi > 0 && rawToV(rawHi) > 2.450f) rawHi--;

  // Bias point and scale for the simulated guitar and the VU full scale.
  int biasRaw = rawLo;
  while (biasRaw < rawHi && rawToV(biasRaw) < 1.650f) biasRaw++;
  const float lsbPerVolt = (rawHi - rawLo) / (2.450f - 0.150f);
  fullScaleLsb = (float)(rawHi - biasRaw);
  GuitarSynth::begin(ACTUAL_RATE, biasRaw, lsbPerVolt);
  AudioDsp::begin(ACTUAL_RATE, fullScaleLsb);   // clean swing above bias

  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1) != pdPASS) return false;
  // The task enables the ADC itself (see audioTask); wait for its verdict.
  for (int i = 0; i < 2000 && enableState == 0; i++) delay(1);
  return enableState == 1;
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

bool AudioInput::startNoiseCalibration() {
  portENTER_CRITICAL(&mux);
  const bool busy = calibrationRequested || calibrationPublished.active;
  if (!busy) calibrationRequested = true;
  portEXIT_CRITICAL(&mux);
  return !busy;
}

AudioInput::Calibration AudioInput::calibration() {
  portENTER_CRITICAL(&mux);
  const Calibration c = calibrationPublished;
  portEXIT_CRITICAL(&mux);
  return c;
}

void AudioInput::requestBatteryRead() { batteryRequested = true; }

AudioInput::BatteryPin AudioInput::batteryPin() {
  portENTER_CRITICAL(&mux);
  const BatteryPin b = batteryPublished;
  portEXIT_CRITICAL(&mux);
  return b;
}

uint32_t AudioInput::lastBatteryReadMs() { return batteryReadAt; }

uint32_t AudioInput::blockCount() { return blocks; }
