#include <Arduino.h>
#include "SPIFFS.h"
#include "driver/i2s.h"

#define I2SR (i2s_port_t)0
#define SAMPLE_RATE 44100       // Même fréquence que ton enregistrement
#define BLOCK_SIZE 1024
#define PW GPIO_NUM_21          // Amp power ON
#define GAIN GPIO_NUM_23

const i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, //doit être cohérent avec le son qu'on veut lire
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = BLOCK_SIZE,
  .use_apll = false
};

const i2s_pin_config_t pin_config = {
  .bck_io_num = 5,
  .ws_io_num = 25,
  .data_out_num = 26,
  .data_in_num = I2S_PIN_NO_CHANGE
};

void setup() {
  Serial.begin(115200);
  Serial.println("Lecture fichier audio...");

  // Activer SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS init failed !");
    return;
  }

  // Amp power enable
  gpio_reset_pin(PW);
  gpio_set_direction(PW, GPIO_MODE_OUTPUT);
  gpio_set_level(PW, 0);

  gpio_reset_pin(GAIN);
  gpio_set_direction(GAIN, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(GAIN, GPIO_PULLDOWN_ONLY);


  // Init I2S
  i2s_driver_install(I2SR, &i2s_config, 0, NULL);
  i2s_set_pin(I2SR, &pin_config);
  i2s_start(I2SR);

  // Ouvrir le fichier
  File f = SPIFFS.open("/audio1.raw", "r");
  if(!f){
    Serial.println("Fichier introuvable !");
    return;
  }

  uint8_t buffer[BLOCK_SIZE];
  size_t bytesWritten;


  // Lire et envoyer sur I2S
  gpio_set_level(PW, 1);
  while(f.available()){
    size_t bytesRead = f.read(buffer, BLOCK_SIZE);
    i2s_write(I2SR, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }
  gpio_set_level(PW, 0);
  

  f.close();
  Serial.println("Lecture terminée !");
}

void loop() {
  // rien à faire
}
