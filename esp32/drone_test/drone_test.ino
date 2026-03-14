#include <driver/i2s.h>
#include <math.h>

#define I2S_BCK  26
#define I2S_WS   33
#define I2S_DOUT 25
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 256

int16_t outBuf[BUFFER_SIZE * 2];
float phase = 0;

void setup() {
  Serial.begin(115200);
  
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
  Serial.println("Simple 440Hz test tone");
}

void loop() {
  for (int s = 0; s < BUFFER_SIZE; s++) {
    int16_t val = (int16_t)(sinf(6.28318f * phase) * 5000.0f);
    outBuf[s * 2] = val;
    outBuf[s * 2 + 1] = val;
    phase += 440.0f / SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
  }
  size_t written;
  i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
}
