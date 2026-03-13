#include <driver/i2s.h>
#include <math.h>

#define I2S_BCK  26
#define I2S_WS   33
#define I2S_DOUT 25

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 512
#define TWO_PI 6.28318530718f

// Sine wavetable
#define TABLE_SIZE 1024
static float sinTable[TABLE_SIZE];

void buildSineTable() {
  for (int i = 0; i < TABLE_SIZE; i++)
    sinTable[i] = sinf(TWO_PI * i / TABLE_SIZE);
}

inline float fastSin(float phase) {
  float idx = phase * TABLE_SIZE;
  int i = (int)idx;
  float frac = idx - i;
  i &= (TABLE_SIZE - 1);
  int j = (i + 1) & (TABLE_SIZE - 1);
  return sinTable[i] + frac * (sinTable[j] - sinTable[i]);
}

// --- Drone Layer ---
#define MAX_OSC 8
#define MAX_SCENES 8

struct Osc {
  float phase, freq, amp, lfoPhase, lfoRate, lfoDepth;
};

struct Scene {
  float amps[MAX_OSC];
};

struct DroneLayer {
  Osc oscs[MAX_OSC];
  int numOsc;
  Scene scenes[MAX_SCENES];
  int numScenes;
  int curScene, nxtScene;
  float morphPos, morphSpeed;
  float volume;  // overall layer volume
};

void initLayer(struct DroneLayer &L, const float *freqs, const float *lfoRates,
               const float *lfoDepths, int nOsc, float vol, float morphSpd) {
  L.numOsc = nOsc;
  L.curScene = 0;
  L.nxtScene = 1;
  L.morphPos = 0;
  L.morphSpeed = morphSpd;
  L.volume = vol;
  for (int i = 0; i < nOsc; i++) {
    L.oscs[i].phase = 0;
    L.oscs[i].freq = freqs[i];
    L.oscs[i].amp = 0;
    L.oscs[i].lfoPhase = (float)i / nOsc;
    L.oscs[i].lfoRate = lfoRates[i];
    L.oscs[i].lfoDepth = lfoDepths[i];
  }
}

// Spectral tilt - very slow band gain modulation
float bandPhase[3] = {0.0f, 0.33f, 0.67f};
const float bandRate[3] = {0.005f, 0.007f, 0.009f};
const float bandMin[3] = {0.3f, 0.4f, 0.3f};
const float bandMax[3] = {1.0f, 1.0f, 1.0f};
float bandGain[3] = {1.0f, 0.6f, 0.5f};

float renderLayer(struct DroneLayer &L, const int *bandMap) {
  float mix = 0;
  for (int i = 0; i < L.numOsc; i++) {
    Osc &o = L.oscs[i];
    float lfo = 1.0f - o.lfoDepth * 0.5f + o.lfoDepth * 0.5f * fastSin(o.lfoPhase);
    mix += fastSin(o.phase) * o.amp * lfo * bandGain[bandMap[i]];
    o.phase += o.freq / SAMPLE_RATE;
    if (o.phase >= 1.0f) o.phase -= 1.0f;
    o.lfoPhase += o.lfoRate / SAMPLE_RATE;
    if (o.lfoPhase >= 1.0f) o.lfoPhase -= 1.0f;
  }
  return mix * L.volume;
}

// Which band each oscillator belongs to (0=bass, 1=mid, 2=high)
const int bandMapA[] = {0, 0, 1, 1, 2, 2, 2, 2};  // Layer A
const int bandMapB[] = {1, 1, 2, 2, 2, 2, 2, 2};  // Layer B (no bass)

void morphLayer(struct DroneLayer &L) {
  L.morphPos += L.morphSpeed * BUFFER_SIZE;
  if (L.morphPos >= 1.0f) {
    L.morphPos = 0;
    L.curScene = L.nxtScene;
    L.nxtScene = (L.nxtScene + 1) % L.numScenes;
  }
  for (int i = 0; i < L.numOsc; i++) {
    float a = L.scenes[L.curScene].amps[i];
    float b = L.scenes[L.nxtScene].amps[i];
    L.oscs[i].amp = a + (b - a) * L.morphPos;
  }
}

// === LAYER A: Root drone (55Hz fundamental) ===
DroneLayer layerA;
const float freqsA[] = {55, 110, 165, 220, 660, 880, 1100, 1320};
const float lfoRatesA[] = {0.03, 0.05, 0.07, 0.09, 0.13, 0.17, 0.19, 0.23};
const float lfoDepthsA[] = {0.06, 0.08, 0.10, 0.12, 0.3, 0.4, 0.4, 0.4};
const Scene scenesA[] = {
  {{1.0, 0.50, 0.30, 0.15, 0.00, 0.03, 0.02, 0.00}},
  {{1.0, 0.55, 0.25, 0.20, 0.00, 0.02, 0.00, 0.03}},
  {{1.0, 0.60, 0.20, 0.25, 0.00, 0.00, 0.01, 0.00}},
  {{1.0, 0.65, 0.15, 0.30, 0.03, 0.00, 0.00, 0.02}},
  {{1.0, 0.60, 0.20, 0.25, 0.02, 0.00, 0.03, 0.00}},
  {{1.0, 0.55, 0.25, 0.20, 0.00, 0.03, 0.00, 0.02}},
  {{1.0, 0.50, 0.30, 0.15, 0.00, 0.00, 0.02, 0.02}},
  {{1.0, 0.45, 0.35, 0.10, 0.02, 0.03, 0.00, 0.00}},
};

// === LAYER B: Ethereal shimmer (220Hz base, octaves above root) ===
// Slightly detuned for very gentle beating against layer A's harmonics
// 220.05Hz = 0.05Hz beat with layer A's 220Hz = ~20 second cycle
DroneLayer layerB;
const float freqsB[] = {220.05, 440.1, 660.15, 880.2, 1100.25, 1320.3, 1760.4, 2200.5};
const float lfoRatesB[] = {0.025, 0.04, 0.06, 0.08, 0.11, 0.15, 0.18, 0.21};
const float lfoDepthsB[] = {0.15, 0.20, 0.25, 0.30, 0.40, 0.45, 0.45, 0.45};
const Scene scenesB[] = {
  {{0.5, 0.25, 0.15, 0.10, 0.06, 0.04, 0.02, 0.01}},
  {{0.4, 0.30, 0.10, 0.12, 0.04, 0.06, 0.01, 0.02}},
  {{0.5, 0.20, 0.18, 0.08, 0.05, 0.03, 0.03, 0.01}},
  {{0.3, 0.35, 0.12, 0.15, 0.03, 0.05, 0.02, 0.02}},
  {{0.5, 0.25, 0.15, 0.10, 0.06, 0.02, 0.03, 0.01}},
  {{0.4, 0.28, 0.14, 0.12, 0.04, 0.04, 0.01, 0.03}},
};

int16_t outBuf[BUFFER_SIZE * 2];

// CPU measurement
unsigned long lastReport = 0;
unsigned long renderUs = 0;
int bufCount = 0;

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
  buildSineTable();

  // Layer A: root drone at 55Hz, morphs slowly
  initLayer(layerA, freqsA, lfoRatesA, lfoDepthsA, 8, 0.30f, 0.000005f);
  layerA.numScenes = 8;
  memcpy(layerA.scenes, scenesA, sizeof(scenesA));
  for (int i = 0; i < 8; i++) layerA.oscs[i].amp = scenesA[0].amps[i];

  // Layer B: ethereal shimmer at 220Hz, quieter, slower morph
  initLayer(layerB, freqsB, lfoRatesB, lfoDepthsB, 8, 0.12f, 0.000003f);
  layerB.numScenes = 6;
  memcpy(layerB.scenes, scenesB, sizeof(scenesB));
  for (int i = 0; i < 8; i++) layerB.oscs[i].amp = scenesB[0].amps[i];

  setupI2S();
  Serial.println("Dual drone playing!");
}

void loop() {
  unsigned long t0 = micros();

  for (int s = 0; s < BUFFER_SIZE; s++) {
    float mix = renderLayer(layerA, bandMapA) + renderLayer(layerB, bandMapB);

    // Soft clip with tanh-like curve
    if (mix > 0.8f) mix = 0.8f + 0.2f * tanhf((mix - 0.8f) * 5.0f);
    if (mix < -0.8f) mix = -0.8f + 0.2f * tanhf((mix + 0.8f) * 5.0f);
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;

    int16_t val = (int16_t)(mix * 10000.0f);
    outBuf[s * 2] = val;
    outBuf[s * 2 + 1] = val;
  }

  unsigned long t1 = micros();
  renderUs += (t1 - t0);
  bufCount++;

  size_t written;
  i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);

  morphLayer(layerA);
  morphLayer(layerB);

  // Update spectral tilt - very slow band gain sweep
  for (int b = 0; b < 3; b++) {
    bandPhase[b] += bandRate[b] / SAMPLE_RATE * BUFFER_SIZE;
    if (bandPhase[b] >= 1.0f) bandPhase[b] -= 1.0f;
    float s = fastSin(bandPhase[b]);  // -1..1
    bandGain[b] = bandMin[b] + (bandMax[b] - bandMin[b]) * (s * 0.5f + 0.5f);
  }

  // Report CPU usage every 5 seconds
  if (millis() - lastReport > 5000) {
    // Time available per buffer: BUFFER_SIZE / SAMPLE_RATE seconds
    float availUs = (float)BUFFER_SIZE / SAMPLE_RATE * 1000000.0f;
    float avgUs = (float)renderUs / bufCount;
    float cpuPct = avgUs / availUs * 100.0f;
    Serial.printf("CPU: %.1f%% (%d us avg, %d us avail per buf)\n",
                  cpuPct, (int)avgUs, (int)availUs);
    renderUs = 0;
    bufCount = 0;
    lastReport = millis();
  }
}
