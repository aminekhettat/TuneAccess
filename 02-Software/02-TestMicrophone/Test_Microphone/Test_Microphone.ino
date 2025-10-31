/*MUSE beep
 
*/

#include "Arduino.h"
#include "SPIFFS.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include <Adafruit_NeoPixel.h>

#define I2SR (i2s_port_t) 1
#define BLOCK_SIZE 1024 // Taille de buffer plus grande pour enregistrer
#define RECORD_TIME 5       // secondes
#define FILE_NAME "/audio1.wav"  // fichier de sortie sur SPIFFS


const uint16_t PixelCount = 1;
const uint8_t PixelPin = 22;
Adafruit_NeoPixel pixels(PixelCount, PixelPin, NEO_GRB + NEO_KHZ800);



const i2s_config_t i2s_configR = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = 44100,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = BLOCK_SIZE, 
  .use_apll = false
};

i2s_pin_config_t pin_configR = {
  .bck_io_num = 5,
  .ws_io_num = 25,
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = 35
};

//void record() {
//
//}


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Enregistrement audio ---");

  // Initialisation SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed");
    while(1);
  }
  Serial.println("SPIFFS initialisé");

  // Installer I2S
  i2s_driver_install(I2SR, &i2s_configR, 0, NULL);
  i2s_set_pin(I2SR, &pin_configR);
  i2s_start(I2SR);

  // Créer fichier pour enregistrer
  File file = SPIFFS.open(FILE_NAME, FILE_WRITE);
  if(!file){
    Serial.println("Impossible d'ouvrir le fichier");
    return;
  }

  Serial.println("Enregistrement en cours...");
  int16_t buffer[1024]; //pour conserver les données recus du micro
  size_t bytes_read;
  unsigned long start = millis();

  //Temps écoulé : 5000 millisecondes
  while((millis() - start) < RECORD_TIME * 1000){
    i2s_read(I2SR, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
    file.write((uint8_t*)buffer, bytes_read);
  }

  //Fermeture du fichier
  file.close();
  Serial.println("Enregistrement terminé !");
  Serial.print("Fichier créé : "); Serial.println(FILE_NAME);
}

void loop() {
  
}
