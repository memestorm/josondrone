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

// Binaural beat: single oscillator that creates L/R difference
float binauralPhase = 0;
const float binauralFreq = 6.0f;  // Hz — theta range

void renderLayerStereo(struct DroneLayer &L, const int *bandMap, float &outL, float &outR) {
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
  float m = mix * L.volume;
  outL += m;
  outR += m;
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

// === Tuning drift: slowly evolve between A440 (55Hz) and Verdi A432 (54Hz) ===
const float tuneA440 = 55.0f;
const float tuneVerdi = 54.0f;  // 432/8
float tunePhase = 0;
const float tuneRate = 0.004f;  // Hz — full cycle ~250 seconds (~4 min round trip)

// Base frequency ratios (relative to fundamental) for retuning
const float ratiosA[] = {1, 2, 3, 4, 12, 16, 20, 24};  // 55, 110, 165, 220, 660, 880, 1100, 1320
const float ratiosB[] = {4.0009f, 8.0018f, 12.0027f, 16.0036f, 20.0045f, 24.0055f, 32.0073f, 40.0091f};
// Layer B ratios include the slight detuning: 220.05/55 = 4.0009 etc.

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

// === BELLS: Fairy dust chime layer ===
#define MAX_BELLS 3       // polyphony (CPU friendly)
#define BELL_PARTIALS 3   // pure DX7-style: fundamental + octave + fifth

// Consonant bell pitches that work with A55Hz drone
const float bellPitches[] = {440.0f, 554.37f, 659.26f, 880.0f, 1108.73f, 1318.51f};
const int numBellPitches = 6;

// Pure integer ratios — clean FM-style bell, no inharmonics
const float bellPartialRatio[BELL_PARTIALS] = {1.0f, 2.0f, 3.0f};
const float bellPartialAmp[BELL_PARTIALS]   = {1.0f, 0.4f, 0.15f};

struct Bell {
  bool active;
  float phase[BELL_PARTIALS];
  float freq;          // fundamental
  float env;           // current envelope value
  float decay;         // decay rate per sample (e.g. 0.99997)
};

Bell bells[MAX_BELLS];
float bellVolume = 0.04f;  // very light fairy dust

// Simple LFSR pseudo-random
uint32_t bellRng = 12345;
uint32_t bellRand() {
  bellRng ^= bellRng << 13;
  bellRng ^= bellRng >> 17;
  bellRng ^= bellRng << 5;
  return bellRng;
}

float bellRandFloat() {
  return (float)(bellRand() & 0xFFFF) / 65536.0f;
}

// Trigger timing
float bellTimer = 0;
float bellNextTime = 0;  // samples until next bell

void initBells() {
  for (int i = 0; i < MAX_BELLS; i++) {
    bells[i].active = false;
  }
  bellNextTime = SAMPLE_RATE * 15.0f;  // first bell after 15 seconds
}

void triggerBell() {
  // Find a free voice
  int slot = -1;
  float quietest = 1.0f;
  int quietSlot = 0;
  for (int i = 0; i < MAX_BELLS; i++) {
    if (!bells[i].active) { slot = i; break; }
    if (bells[i].env < quietest) { quietest = bells[i].env; quietSlot = i; }
  }
  if (slot < 0) slot = quietSlot;  // steal quietest voice

  Bell &b = bells[slot];
  b.active = true;
  b.freq = bellPitches[bellRand() % numBellPitches];
  // Fixed long decay: ~12 second tail
  float decaySec = 12.0f;
  b.decay = powf(0.001f, 1.0f / (decaySec * SAMPLE_RATE));  // -60dB over decaySec
  b.env = 0.7f + bellRandFloat() * 0.3f;  // slight random velocity
  for (int p = 0; p < BELL_PARTIALS; p++) {
    b.phase[p] = bellRandFloat() * 0.1f;  // slight random phase offset
  }
}

float renderBells() {
  float mix = 0;
  for (int i = 0; i < MAX_BELLS; i++) {
    if (!bells[i].active) continue;
    Bell &b = bells[i];
    float sig = 0;
    for (int p = 0; p < BELL_PARTIALS; p++) {
      sig += fastSin(b.phase[p]) * bellPartialAmp[p];
      b.phase[p] += (b.freq * bellPartialRatio[p]) / SAMPLE_RATE;
      if (b.phase[p] >= 1.0f) b.phase[p] -= 1.0f;
    }
    mix += sig * b.env;
    b.env *= b.decay;
    if (b.env < 0.0001f) b.active = false;  // voice done
  }
  return mix * bellVolume;
}

// === REVERB: 4 comb filters + 2 allpass filters ===
// Prime-length delays to avoid metallic resonances
#define NUM_COMBS 4
#define NUM_ALLPASS 2

const int combLens[NUM_COMBS] = {1687, 1931, 2143, 2473};  // ~38-56ms
const float combFB[NUM_COMBS] = {0.84f, 0.82f, 0.80f, 0.78f};

const int apLens[NUM_ALLPASS] = {347, 521};  // ~8-12ms
const float apFB = 0.5f;

// Delay buffers
float combBuf[NUM_COMBS][2500];  // max comb length
float apBuf[NUM_ALLPASS][600];   // max allpass length
int combIdx[NUM_COMBS] = {0};
int apIdx[NUM_ALLPASS] = {0};

// Low-pass filter state per comb (darkens reverb tail)
float combLP[NUM_COMBS] = {0};
const float lpCoeff = 0.4f;  // lower = darker tail

float reverbMix = 0.25f;  // wet/dry mix

float processReverb(float input) {
  float combOut = 0;

  // Parallel comb filters with LP in feedback
  for (int c = 0; c < NUM_COMBS; c++) {
    float delayed = combBuf[c][combIdx[c]];
    // One-pole low-pass in feedback path
    combLP[c] = combLP[c] + lpCoeff * (delayed - combLP[c]);
    combBuf[c][combIdx[c]] = input + combLP[c] * combFB[c];
    combIdx[c] = (combIdx[c] + 1) % combLens[c];
    combOut += delayed;
  }
  combOut *= 0.25f;  // average the 4 combs

  // Series allpass filters (add diffusion)
  float ap = combOut;
  for (int a = 0; a < NUM_ALLPASS; a++) {
    float delayed = apBuf[a][apIdx[a]];
    float temp = -ap * apFB + delayed;
    apBuf[a][apIdx[a]] = ap + delayed * apFB;
    apIdx[a] = (apIdx[a] + 1) % apLens[a];
    ap = temp;
  }

  return ap;
}

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

  // Clear reverb buffers
  memset(combBuf, 0, sizeof(combBuf));
  memset(apBuf, 0, sizeof(apBuf));

  initBells();

  setupI2S();
  Serial.println("Dual drone + reverb + fairy dust + binaural beats playing!");
}

void loop() {
  unsigned long t0 = micros();

  for (int s = 0; s < BUFFER_SIZE; s++) {
    // Bell trigger check
    bellTimer += 1.0f;
    if (bellTimer >= bellNextTime) {
      triggerBell();
      bellTimer = 0;
      // Next bell in 15-25 seconds (very sparse fairy dust)
      bellNextTime = SAMPLE_RATE * (15.0f + bellRandFloat() * 10.0f);
    }

    float dryL = 0, dryR = 0;
    renderLayerStereo(layerA, bandMapA, dryL, dryR);
    renderLayerStereo(layerB, bandMapB, dryL, dryR);
    float chime = renderBells();
    dryL += chime;
    dryR += chime;

    // Apply binaural beat: gentle amplitude modulation, opposite phase L vs R
    // This creates the perceptual binaural beating in headphones
    float bin = fastSin(binauralPhase) * 0.15f;  // subtle 15% modulation depth
    binauralPhase += binauralFreq / SAMPLE_RATE;
    if (binauralPhase >= 1.0f) binauralPhase -= 1.0f;
    dryL *= (1.0f + bin);
    dryR *= (1.0f - bin);

    // Reverb on mono sum (saves CPU, reverb naturally blends stereo)
    float wet = processReverb((dryL + dryR) * 0.5f);
    float mixL = dryL + wet * reverbMix;
    float mixR = dryR + wet * reverbMix;

    // Soft clip both channels
    if (mixL > 0.8f) mixL = 0.8f + 0.2f * tanhf((mixL - 0.8f) * 5.0f);
    if (mixL < -0.8f) mixL = -0.8f + 0.2f * tanhf((mixL + 0.8f) * 5.0f);
    if (mixL > 1.0f) mixL = 1.0f;
    if (mixL < -1.0f) mixL = -1.0f;
    if (mixR > 0.8f) mixR = 0.8f + 0.2f * tanhf((mixR - 0.8f) * 5.0f);
    if (mixR < -0.8f) mixR = -0.8f + 0.2f * tanhf((mixR + 0.8f) * 5.0f);
    if (mixR > 1.0f) mixR = 1.0f;
    if (mixR < -1.0f) mixR = -1.0f;

    outBuf[s * 2] = (int16_t)(mixL * 8000.0f);
    outBuf[s * 2 + 1] = (int16_t)(mixR * 8000.0f);
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

  // Update tuning drift: slowly morph between A440 and Verdi A432
  tunePhase += tuneRate / SAMPLE_RATE * BUFFER_SIZE;
  if (tunePhase >= 1.0f) tunePhase -= 1.0f;
  float tuneMix = fastSin(tunePhase) * 0.5f + 0.5f;  // 0..1
  float baseFreq = tuneA440 + (tuneVerdi - tuneA440) * tuneMix;
  for (int i = 0; i < 8; i++) {
    layerA.oscs[i].freq = baseFreq * ratiosA[i];
    layerB.oscs[i].freq = baseFreq * ratiosB[i];
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
