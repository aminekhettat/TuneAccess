/* YIN Accordeur ESP32 I2S */

// *** 1. Inclure les librairies nécessaires ***
#include "Arduino.h"
#include "driver/i2s.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>


// *** 2. Constantes I2S et YIN (Mises à jour) ***
#define I2S_PORT (i2s_port_t) 0 // Typiquement I2S_NUM_0 (Port 0) est utilisé.
                                 // Nous utiliserons le port 0 car le port 1 est souvent utilisé pour le DAC.
#define SAMPLE_RATE 22050        // Fréquence d'échantillonnage optimisée pour YIN
#define BLOCK_SIZE 1024          // Taille du buffer d'analyse pour YIN
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT
#define YIN_THRESHOLD 0.05    // Seuil pour la fonction de différence.

// *** 3. Configuration des Broches ***
// Vérifiez que ces broches sont correctes pour votre carte MUSO PROTO !
i2s_pin_config_t pin_config_rx = {
  .bck_io_num = 5,
  .ws_io_num = 25,
  .data_out_num = I2S_PIN_NO_CHANGE, // RX: pas de sortie
  .data_in_num = 35                  // Data In (Micro)
};

// Configuration I2S
const i2s_config_t i2s_config_rx = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = BITS_PER_SAMPLE,
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // Mono (pour un micro)
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
  .intr_alloc_flags = 0,
  .dma_buf_count = 4,
  .dma_buf_len = BLOCK_SIZE,
  .use_apll = false
};

// *** 4. Buffers de Données ***
// I2S lit en 16 bits (selon config), mais nous utilisons 32 bits par sécurité pour le DMA
int32_t raw_i2s_buffer[BLOCK_SIZE]; 
int16_t audio_buffer_16bit[BLOCK_SIZE]; // Buffer pour l'analyse YIN

// --- Configuration Neopixel (non modifié) ---
const uint16_t PixelCount = 1;
const uint8_t PixelPin = 22;
Adafruit_NeoPixel pixels(PixelCount, PixelPin, NEO_GRB + NEO_KHZ800);
// ---------------------------------------------

// Implémentation complète de l'algorithme YIN
float Yin_detect_pitch(int16_t* audioBuffer, uint16_t bufferSize, uint32_t sampleRate) {
    uint16_t tau_max = bufferSize / 2;
    float diff[tau_max]; 
    float cumulativeSum = 0.0;
    
    // 1. Fonction de Différence (d(tau))
    for (uint16_t tau = 0; tau < tau_max; tau++) {
        diff[tau] = 0.0;
        for (uint16_t j = 0; j < bufferSize - tau; j++) {
            float delta = (float)audioBuffer[j] - audioBuffer[j + tau];
            diff[tau] += delta * delta;
        }
    }

    // 2. Fonction de Différence Cumulée Normalisée (d'(tau)) 
    diff[0] = 1.0; 
    
    for (uint16_t tau = 1; tau < tau_max; tau++) {
        cumulativeSum += diff[tau]; 
        
        if (cumulativeSum > 0) {
            diff[tau] = diff[tau] * tau / cumulativeSum;
        } else {
            diff[tau] = 1.0; 
        }
    }
    
    // 3. Recherche du Minimum Absolu (Période)
    const uint16_t min_tau = (sampleRate / 1200); // Ex: Min 1200Hz -> tau min
    const uint16_t max_tau = (sampleRate / 80);   // Ex: Max 80Hz -> tau max
    
    uint16_t period = 0;
    for (uint16_t tau = min_tau; tau < max_tau; tau++) {
        if (diff[tau] < YIN_THRESHOLD) {
            if (diff[tau] < diff[tau - 1]) {
                period = tau;
                break; 
            }
        }
    }
    
    if (period == 0 || period >= tau_max - 1) {
        return 0.0;
    }

    // 4. Interpolation Quadratique
    float a = diff[period - 1];
    float b = diff[period];
    float c = diff[period + 1];
    float p_offset = 0.5 * (a - c) / (a - 2.0 * b + c);
    
    float exact_period = (float)period + p_offset;

    // 5. Calcul de la Fréquence
    return (float)sampleRate / exact_period; 
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Accordeur YIN I2S ---");

  // Initialisation Neopixel
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 50)); // Bleu doux : Prêt
  pixels.show();

  // Installer I2S sur le port 0
  i2s_driver_install(I2S_PORT, &i2s_config_rx, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config_rx);
  i2s_start(I2S_PORT);
  Serial.println("I2S initialisé pour l'écoute en temps réel.");
}


void loop() {
  size_t bytes_read;
  
  // 1. Lire les échantillons du micro via I2S
  // On lit un bloc du micro dans le buffer DMA (raw_i2s_buffer)
  i2s_read(I2S_PORT, raw_i2s_buffer, sizeof(raw_i2s_buffer), &bytes_read, portMAX_DELAY);

  if (bytes_read == sizeof(raw_i2s_buffer)) {
    // 2. Prétraitement : Conversion et centrage du signal
    // Décalage pour obtenir les 16 bits audio utiles et centrer autour de 0
    // L'ajustement du décalage (ici >> 14) peut dépendre du volume de votre micro.
    for (int i = 0; i < BLOCK_SIZE; i++) {
      // On extrait les 16 bits les plus significatifs et on caste en 16 bits signé
      // Note: Le décalage exact (ex: >> 14) dépend du micro.
      audio_buffer_16bit[i] = (raw_i2s_buffer[i] >> 16); 
    }
    
    // 3. Détection de la fréquence fondamentale (F0) avec YIN
    float frequency = Yin_detect_pitch(audio_buffer_16bit, BLOCK_SIZE, SAMPLE_RATE);

    // 4. Affichage du résultat et logique d'accordage
    if (frequency > 50.0 && frequency < 1500.0) { 
      // Si une fréquence valide est détectée (typiquement instruments) : 50 pour ignorer les bruits et les fausses détections, 1500 pour éviter de détecter accidentellement des harmoniques de haut niveau 
      Serial.print("Frequence detectee: ");
      Serial.print(frequency, 2); // Afficher 2 décimales
      Serial.println(" Hz");
      pixels.setPixelColor(0, pixels.Color(0, 0, 50)); // Bleu 
      pixels.show();

    } else {
      // Pas de son ou bruit, fréquence non détectée
      //Serial.println("...");
      pixels.setPixelColor(0, pixels.Color(50, 0, 0)); // Rouge
      pixels.show();
    }
  }
}
