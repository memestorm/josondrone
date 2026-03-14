#include <driver/i2s.h>
#include <math.h>

#define I2S_BCK  26
#define I2S_WS   33
#define I2S_DOUT 25

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 512

// === WAVETABLE ===
#define TABLE_SIZE 1024
static float sinTable[TABLE_SIZE];

void buildSineTable() {
  for (int i = 0; i < TABLE_SIZE; i++)
    sinTable[i] = sinf(6.28318530718f * i / TABLE_SIZE);
}

inline float fastSin(float phase) {
  float idx = phase * TABLE_SIZE;
  int i = (int)idx;
  float frac = idx - i;
  i &= (TABLE_SIZE - 1);
  int j = (i + 1) & (TABLE_SIZE - 1);
  return sinTable[i] + frac * (sinTable[j] - sinTable[i]);
}

// === RANDOM ===
uint32_t rng = 98765;
uint32_t xorRand() {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}
float randFloat() { return (float)(xorRand() & 0xFFFF) / 65536.0f; }
bool chance(float pct) { return randFloat() < pct; }

// === NOISE ===
uint32_t noiseState = 22222;
float noise() {
  noiseState ^= noiseState << 13;
  noiseState ^= noiseState >> 17;
  noiseState ^= noiseState << 5;
  return (float)(int32_t)noiseState / 2147483648.0f;
}

// === SEQUENCER ===
#define NUM_PARTS 5
#define NUM_STEPS 32
#define KICK   0
#define SNARE  1
#define HIHAT  2
#define CLAP   3
#define COWBELL 4

float bpm = 100.0f;
float samplesPerStep;
float stepCounter = 0;
int currentStep = 0;

uint32_t pattern[NUM_PARTS];
uint32_t hatOpen;  // bitmask: 1 = open hat on that step

// Sound parameter variables (randomized per cycle)
float kickClickAmt = 0.7f;
float hatBaseFreq = 540.0f;
float cowFreq1 = 540.0f, cowFreq2 = 800.0f;
float snareDecayTime = 0.15f;
float snareNoiseDecayTime = 0.2f;

// === 808 KICK ===
struct { bool active; float phase, freq, freqEnd, amp, ampDecay, freqDecay, click, clickDecay; } kick;

void triggerKick() {
  kick.active = true;
  kick.phase = 0;
  kick.freq = kick.freqEnd * 3.3f;
  kick.amp = 1.0f;
  kick.freqDecay = powf(0.001f, 1.0f / (0.08f * SAMPLE_RATE));
  kick.click = kickClickAmt;
  kick.clickDecay = powf(0.001f, 1.0f / (0.004f * SAMPLE_RATE));
}

float renderKick() {
  if (!kick.active) return 0;
  float sig = fastSin(kick.phase) * kick.amp;
  sig += kick.click * (fastSin(kick.phase * 7.5f) * 0.5f + fastSin(kick.phase * 13.3f) * 0.3f);
  kick.phase += kick.freq / SAMPLE_RATE;
  if (kick.phase >= 1.0f) kick.phase -= 1.0f;
  kick.freq = kick.freqEnd + (kick.freq - kick.freqEnd) * kick.freqDecay;
  kick.amp *= kick.ampDecay;
  kick.click *= kick.clickDecay;
  if (kick.amp < 0.0001f) kick.active = false;
  return sig;
}

// === 808 SNARE ===
struct {
  bool active;
  float phase, freq, amp, ampDecay;
  float noiseAmp, noiseDecay;
  float noiseLp;  // low-pass state for filtered noise
} snare;

void triggerSnare() {
  snare.active = true;
  snare.phase = 0;
  snare.amp = 0.8f;
  snare.ampDecay = powf(0.001f, 1.0f / (snareDecayTime * SAMPLE_RATE));
  snare.noiseAmp = 1.0f;
  snare.noiseDecay = powf(0.001f, 1.0f / (snareNoiseDecayTime * SAMPLE_RATE));
  snare.noiseLp = 0;
}

float renderSnare() {
  if (!snare.active) return 0;
  // Tone: two detuned sines for body
  float tone = fastSin(snare.phase) * 0.6f + fastSin(snare.phase * 1.47f) * 0.4f;
  tone *= snare.amp;
  // Noise: filtered white noise for snap
  float n = noise();
  snare.noiseLp += 0.4f * (n - snare.noiseLp);  // LP filter
  float snap = snare.noiseLp * snare.noiseAmp;
  snare.phase += snare.freq / SAMPLE_RATE;
  if (snare.phase >= 1.0f) snare.phase -= 1.0f;
  snare.amp *= snare.ampDecay;
  snare.noiseAmp *= snare.noiseDecay;
  if (snare.amp < 0.0001f && snare.noiseAmp < 0.0001f) snare.active = false;
  return (tone + snap) * 0.7f;
}

// === 808 HI-HAT ===
struct {
  bool active;
  float amp, decay;
  float phases[6];  // 6 square waves at metallic ratios
  float lp;
  float hp, hpPrev;  // high-pass state
} hat;

const float hatRatios[] = {1.0f, 1.4142f, 1.6818f, 1.9318f, 2.1441f, 2.6614f};

void triggerHat(bool open) {
  hat.active = true;
  hat.amp = 0.9f;
  float decayTime = open ? 0.35f : 0.05f;
  hat.decay = powf(0.001f, 1.0f / (decayTime * SAMPLE_RATE));
  hat.lp = 0;
  hat.hp = 0;
  hat.hpPrev = 0;
  for (int i = 0; i < 6; i++) hat.phases[i] = randFloat();
}

float renderHat() {
  if (!hat.active) return 0;
  // 6 metallic square waves summed (808 hat topology)
  float baseFreq = hatBaseFreq;
  float sig = 0;
  for (int i = 0; i < 6; i++) {
    sig += (hat.phases[i] < 0.5f) ? 1.0f : -1.0f;
    hat.phases[i] += (baseFreq * hatRatios[i]) / SAMPLE_RATE;
    if (hat.phases[i] >= 1.0f) hat.phases[i] -= 1.0f;
  }
  sig *= 0.16f;  // normalize
  // Band-pass: LP then HP for that metallic sizzle
  hat.lp += 0.3f * (sig - hat.lp);
  float hp = hat.lp - hat.hpPrev;
  hat.hpPrev = hat.lp * 0.98f;
  float out = hp * hat.amp;
  hat.amp *= hat.decay;
  if (hat.amp < 0.0001f) hat.active = false;
  return out;
}

// === 808 CLAP ===
struct {
  bool active;
  float amp, decay;
  float burstTimer;  // multiple micro-bursts for "spread"
  int burstCount;
  float envAmp;
  float lp;
} clap;

void triggerClap() {
  clap.active = true;
  clap.amp = 1.0f;
  clap.decay = powf(0.001f, 1.0f / (0.3f * SAMPLE_RATE));
  clap.burstTimer = 0;
  clap.burstCount = 0;
  clap.envAmp = 1.0f;
  clap.lp = 0;
}

float renderClap() {
  if (!clap.active) return 0;
  // Multiple noise bursts (the "clap" spread)
  float n = noise();
  clap.lp += 0.35f * (n - clap.lp);  // band-limited noise
  float sig = clap.lp;
  // 4 micro-bursts in first ~20ms, then sustained decay
  if (clap.burstCount < 4) {
    clap.burstTimer += 1.0f;
    float burstLen = SAMPLE_RATE * 0.005f;  // 5ms per burst
    float burstGap = SAMPLE_RATE * 0.007f;  // 7ms gap
    float pos = fmodf(clap.burstTimer, burstLen + burstGap);
    if (pos > burstLen) sig *= 0.1f;  // quiet in gaps
    if (clap.burstTimer > (burstLen + burstGap) * (clap.burstCount + 1))
      clap.burstCount++;
    sig *= 1.2f;  // bursts are louder
  }
  sig *= clap.amp;
  clap.amp *= clap.decay;
  if (clap.amp < 0.0001f) clap.active = false;
  return sig * 0.6f;
}

// === COWBELL ===
struct {
  bool active;
  float phase1, phase2;
  float amp, decay;
} cowbell;

void triggerCowbell() {
  cowbell.active = true;
  cowbell.phase1 = 0;
  cowbell.phase2 = 0;
  cowbell.amp = 0.8f;
  cowbell.decay = powf(0.001f, 1.0f / (0.08f * SAMPLE_RATE));
}

float renderCowbell() {
  if (!cowbell.active) return 0;
  // Two detuned square-ish waves (classic 808 cowbell)
  float s1 = fastSin(cowbell.phase1);
  float s2 = fastSin(cowbell.phase2);
  // Slight squaring for metallic edge
  s1 = s1 > 0 ? 0.7f : -0.7f;
  s2 = s2 > 0 ? 0.7f : -0.7f;
  float sig = (s1 + s2) * 0.5f * cowbell.amp;
  cowbell.phase1 += cowFreq1 / SAMPLE_RATE;
  if (cowbell.phase1 >= 1.0f) cowbell.phase1 -= 1.0f;
  cowbell.phase2 += cowFreq2 / SAMPLE_RATE;
  if (cowbell.phase2 >= 1.0f) cowbell.phase2 -= 1.0f;
  cowbell.amp *= cowbell.decay;
  if (cowbell.amp < 0.0001f) cowbell.active = false;
  return sig * 0.5f;
}

// === DEFAULT PATTERN ===
void setDefaultPattern() {
  // Kick: every quarter note (steps 0,4,8,12,16,20,24,28)
  pattern[KICK] = 0x11111111;
  // Snare: beats 2,4 of each bar (steps 4,12,20,28)
  pattern[SNARE] = (1<<4)|(1<<12)|(1<<20)|(1<<28);
  // Hi-hat: every 8th note
  pattern[HIHAT] = 0x55555555;
  // Clap: beat 4 of each bar
  pattern[CLAP] = (1<<12)|(1<<28);
  // Cowbell: silent
  pattern[COWBELL] = 0;
  // Hat openness: "and" of 2 and 4
  hatOpen = (1<<6)|(1<<14)|(1<<22)|(1<<30);
}

// === MUSICALLY-WEIGHTED RANDOMIZERS ===
void randomizeKick() {
  uint32_t p = (1<<0)|(1<<16);  // always beat 1 of each bar
  for (int s = 0; s < 32; s++) {
    if (s == 0 || s == 16) continue;
    bool onBeat = (s % 4 == 0);
    bool on8th = (s % 2 == 0);
    if (onBeat && chance(0.55f)) p |= (1<<s);
    else if (on8th && chance(0.12f)) p |= (1<<s);
    else if (chance(0.03f)) p |= (1<<s);
  }
  pattern[KICK] = p;
}

void randomizeSnare() {
  uint32_t p = 0;
  if (chance(0.85f)) p |= (1<<4);
  if (chance(0.90f)) p |= (1<<12);
  if (chance(0.85f)) p |= (1<<20);
  if (chance(0.90f)) p |= (1<<28);
  for (int s = 0; s < 32; s++) {
    if (p & (1<<s)) continue;
    bool nearHit = false;
    for (int d = -2; d <= 2; d++) {
      int ns = (s + d + 32) % 32;
      if (p & (1<<ns)) nearHit = true;
    }
    if (nearHit && chance(0.15f)) p |= (1<<s);
    else if (chance(0.04f)) p |= (1<<s);
  }
  pattern[SNARE] = p;
}

void randomizeHihat() {
  uint32_t p = 0;
  uint32_t ho = 0;
  for (int s = 0; s < 32; s++) {
    bool on8th = (s % 2 == 0);
    bool onBeat = (s % 4 == 0);
    if (on8th) { if (chance(0.92f)) p |= (1<<s); }
    else { if (chance(0.35f)) p |= (1<<s); }
    if ((p & (1<<s)) && !onBeat && chance(0.20f)) ho |= (1<<s);
  }
  pattern[HIHAT] = p;
  hatOpen = ho;
}

void randomizeClap() {
  uint32_t p = 0;
  if (chance(0.80f)) p |= (1<<12);
  if (chance(0.80f)) p |= (1<<28);
  if (chance(0.30f)) p |= (1<<4);
  if (chance(0.30f)) p |= (1<<20);
  for (int s = 0; s < 32; s++) {
    if (p & (1<<s)) continue;
    if (s % 2 == 0 && chance(0.06f)) p |= (1<<s);
  }
  pattern[CLAP] = p;
}

void randomizeCowbell() {
  uint32_t p = 0;
  for (int s = 0; s < 32; s++) {
    bool offbeat = (s % 4 == 2);
    bool on8th = (s % 2 == 0);
    if (offbeat && chance(0.25f)) p |= (1<<s);
    else if (on8th && chance(0.10f)) p |= (1<<s);
    else if (chance(0.02f)) p |= (1<<s);
  }
  pattern[COWBELL] = p;
}

// === TRIGGER DISPATCH ===
// Per-trigger parameter variation
void randomizeAllSounds() {
  kick.freqEnd = 35.0f + randFloat() * 35.0f;
  kick.ampDecay = powf(0.001f, 1.0f / ((0.2f + randFloat() * 0.4f) * SAMPLE_RATE));
  kickClickAmt = 0.3f + randFloat() * 0.7f;
  hatBaseFreq = 400.0f + randFloat() * 300.0f;
  cowFreq1 = 400.0f + randFloat() * 200.0f;
  cowFreq2 = cowFreq1 * (1.3f + randFloat() * 0.4f);
  snareDecayTime = 0.08f + randFloat() * 0.2f;
  snareNoiseDecayTime = 0.1f + randFloat() * 0.25f;
  snare.freq = 140.0f + randFloat() * 100.0f;
  Serial.printf("Sounds: kick=%.0fHz snare=%.0fHz hat=%.0fHz cow=%.0f/%.0fHz click=%.1f\n",
                kick.freqEnd, snare.freq, hatBaseFreq, cowFreq1, cowFreq2, kickClickAmt);
}

void triggerStep(int step) {
  uint32_t mask = (1 << step);

  // At the start of each 2-bar cycle, randomize sound params
  if (step == 0) {
    randomizeAllSounds();
  }

  // Fill zone: last beat of the 2-bar cycle (steps 28-31)
  if (step >= 28) {
    if (!(pattern[KICK] & mask) && chance(0.35f)) triggerKick();
    if (!(pattern[SNARE] & mask) && chance(0.45f)) triggerSnare();
    if (!(pattern[HIHAT] & mask) && chance(0.50f)) triggerHat(chance(0.4f));
    if (!(pattern[CLAP] & mask) && chance(0.20f)) triggerClap();
    if (!(pattern[COWBELL] & mask) && chance(0.15f)) triggerCowbell();
  }

  if (pattern[KICK] & mask) triggerKick();
  if (pattern[SNARE] & mask) triggerSnare();
  if (pattern[HIHAT] & mask) triggerHat((hatOpen & mask) != 0);
  if (pattern[CLAP] & mask) triggerClap();
  if (pattern[COWBELL] & mask) triggerCowbell();
}

// === I2S ===
int16_t outBuf[BUFFER_SIZE * 2];

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

// CPU measurement
unsigned long lastReport = 0;
unsigned long renderUs = 0;
int bufCount = 0;

void setup() {
  Serial.begin(115200);
  buildSineTable();
  setDefaultPattern();
  kick.freqEnd = 45.0f;
  kick.ampDecay = powf(0.001f, 1.0f / (0.4f * SAMPLE_RATE));
  snare.freq = 180.0f;
  samplesPerStep = SAMPLE_RATE * 60.0f / bpm / 4.0f;  // 16th note duration
  stepCounter = samplesPerStep;  // trigger step 0 immediately
  setupI2S();
  Serial.println("808 Drum Machine - 32 step sequencer");
  Serial.printf("BPM: %.0f, samples/step: %.0f\n", bpm, samplesPerStep);
}

void loop() {
  unsigned long t0 = micros();

  for (int s = 0; s < BUFFER_SIZE; s++) {
    stepCounter += 1.0f;
    if (stepCounter >= samplesPerStep) {
      stepCounter -= samplesPerStep;
      triggerStep(currentStep);
      currentStep = (currentStep + 1) % NUM_STEPS;
    }

    float mix = renderKick() * 1.0f
              + renderSnare() * 0.8f
              + renderHat() * 0.6f
              + renderClap() * 0.7f
              + renderCowbell() * 0.5f;

    // Soft clip
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;

    int16_t val = (int16_t)(mix * 14000.0f);
    outBuf[s * 2] = val;
    outBuf[s * 2 + 1] = val;
  }

  unsigned long t1 = micros();
  renderUs += (t1 - t0);
  bufCount++;

  size_t written;
  i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);

  if (millis() - lastReport > 5000) {
    float availUs = (float)BUFFER_SIZE / SAMPLE_RATE * 1000000.0f;
    float avgUs = (float)renderUs / bufCount;
    float cpuPct = avgUs / availUs * 100.0f;
    Serial.printf("CPU: %.1f%% | Step: %d/%d\n", cpuPct, currentStep, NUM_STEPS);
    renderUs = 0;
    bufCount = 0;
    lastReport = millis();
  }
}
