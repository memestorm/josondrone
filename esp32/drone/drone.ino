#include <driver/i2s.h>
#include <math.h>

// I2S pins matching our wiring
#define I2S_BCK  26
#define I2S_WS   33
#define I2S_DOUT 25

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 512
#define TWO_PI 6.28318530718f

// Sine wavetable - 1024 entries for smooth interpolation
#define TABLE_SIZE 1024
static float sinTable[TABLE_SIZE];

void buildSineTable() {
  for (int i = 0; i < TABLE_SIZE; i++) {
    sinTable[i] = sinf(TWO_PI * i / TABLE_SIZE);
  }
}

// Fast sine lookup with linear interpolation
inline float fastSin(float phase) {
  float idx = phase * TABLE_SIZE;
  int i = (int)idx;
  float frac = idx - i;
  i &= (TABLE_SIZE - 1);
  int j = (i + 1) & (TABLE_SIZE - 1);
  return sinTable[i] + frac * (sinTable[j] - sinTable[i]);
}

// Oscillator with phase accumulator
struct Osc {
  float phase;     // 0..1
  float freq;      // Hz
  float amp;       // current amplitude
  float targetAmp; // amplitude we're morphing toward
  float lfoPhase;  // per-osc LFO phase
  float lfoRate;   // LFO speed (Hz)
  float lfoDepth;  // 0..1 how much LFO affects amplitude
};

#define NUM_OSC 8
Osc oscs[NUM_OSC];

// Scene definitions: 8 harmonics x 8 scenes
// {freq, amp} pairs per scene
struct Scene {
  float amps[NUM_OSC]; // amplitude for each oscillator
};

// Oscillator frequencies (all multiples of 55Hz)
const float oscFreqs[NUM_OSC] = {
  55.0f, 110.0f, 165.0f, 220.0f, 660.0f, 880.0f, 1100.0f, 1320.0f
};

// LFO rates per oscillator (different rates = phasing)
const float lfoRates[NUM_OSC] = {
  0.07f, 0.11f, 0.13f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f
};

// LFO depths per oscillator (higher harmonics modulate more)
const float lfoDepths[NUM_OSC] = {
  0.08f, 0.12f, 0.15f, 0.18f, 0.4f, 0.5f, 0.5f, 0.5f
};

const Scene scenes[] = {
  {{1.0f, 0.50f, 0.30f, 0.15f, 0.00f, 0.03f, 0.02f, 0.00f}},
  {{1.0f, 0.55f, 0.25f, 0.20f, 0.00f, 0.02f, 0.00f, 0.03f}},
  {{1.0f, 0.60f, 0.20f, 0.25f, 0.00f, 0.00f, 0.01f, 0.00f}},
  {{1.0f, 0.65f, 0.15f, 0.30f, 0.03f, 0.00f, 0.00f, 0.02f}},
  {{1.0f, 0.60f, 0.20f, 0.25f, 0.02f, 0.00f, 0.03f, 0.00f}},
  {{1.0f, 0.55f, 0.25f, 0.20f, 0.00f, 0.03f, 0.00f, 0.02f}},
  {{1.0f, 0.50f, 0.30f, 0.15f, 0.00f, 0.00f, 0.02f, 0.02f}},
  {{1.0f, 0.45f, 0.35f, 0.10f, 0.02f, 0.03f, 0.00f, 0.00f}},
};
const int NUM_SCENES = sizeof(scenes) / sizeof(scenes[0]);

int currentScene = 0;
int nextScene = 1;
float morphProgress = 0.0f;       // 0..1
float morphSpeed = 0.00001f;      // very slow morph

int16_t outBuf[BUFFER_SIZE * 2]; // stereo interleaved

void setupI2S() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Building sine table...");
  buildSineTable();

  // Init oscillators
  for (int i = 0; i < NUM_OSC; i++) {
    oscs[i].phase = 0.0f;
    oscs[i].freq = oscFreqs[i];
    oscs[i].amp = scenes[0].amps[i];
    oscs[i].targetAmp = scenes[0].amps[i];
    oscs[i].lfoPhase = (float)i / NUM_OSC; // spread LFO phases
    oscs[i].lfoRate = lfoRates[i];
    oscs[i].lfoDepth = lfoDepths[i];
  }

  Serial.println("Starting I2S...");
  setupI2S();
  Serial.println("Playing drone!");
}

void loop() {
  float phaseInc[NUM_OSC];
  float lfoInc[NUM_OSC];

  for (int i = 0; i < NUM_OSC; i++) {
    phaseInc[i] = oscs[i].freq / SAMPLE_RATE;
    lfoInc[i] = oscs[i].lfoRate / SAMPLE_RATE;
  }

  // Fill buffer
  for (int s = 0; s < BUFFER_SIZE; s++) {
    float mix = 0.0f;

    for (int i = 0; i < NUM_OSC; i++) {
      // LFO modulates amplitude
      float lfo = 1.0f - oscs[i].lfoDepth * 0.5f
                  + oscs[i].lfoDepth * 0.5f * fastSin(oscs[i].lfoPhase);

      float sample = fastSin(oscs[i].phase) * oscs[i].amp * lfo;
      mix += sample;

      oscs[i].phase += phaseInc[i];
      if (oscs[i].phase >= 1.0f) oscs[i].phase -= 1.0f;

      oscs[i].lfoPhase += lfoInc[i];
      if (oscs[i].lfoPhase >= 1.0f) oscs[i].lfoPhase -= 1.0f;
    }

    // Soft clip and scale
    mix *= 0.35f;
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;

    int16_t val = (int16_t)(mix * 12000.0f);
    outBuf[s * 2] = val;      // left
    outBuf[s * 2 + 1] = val;  // right
  }

  // Write to I2S
  size_t written;
  i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);

  // Slowly morph between scenes
  morphProgress += morphSpeed * BUFFER_SIZE;
  if (morphProgress >= 1.0f) {
    morphProgress = 0.0f;
    currentScene = nextScene;
    nextScene = (nextScene + 1) % NUM_SCENES;
  }

  // Interpolate target amplitudes
  for (int i = 0; i < NUM_OSC; i++) {
    float a = scenes[currentScene].amps[i];
    float b = scenes[nextScene].amps[i];
    oscs[i].amp = a + (b - a) * morphProgress;
  }
}
