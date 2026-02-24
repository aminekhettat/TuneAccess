/* YIN Accordeur ESP32 GBF*/

// *** 1. Inclure les librairies nécessaires ***
#include <Arduino.h>
#include <driver/dac.h>
#include <Adafruit_NeoPixel.h>
#include <stdint.h>


// *** 2. Constantes I2S et YIN (Mises à jour) ***
#define I2S_PORT (i2s_port_t) 0 // Typiquement I2S_NUM_0 (Port 0) est utilisé.
                                 // Nous utiliserons le port 0 car le port 1 est souvent utilisé pour le DAC.
#define SAMPLE_RATE 16000        // Fréquence d'échantillonnage optimisée pour YIN
#define BLOCK_SIZE 1024          // Taille du buffer d'analyse pour YIN
#define ADC_PIN 35
#define YIN_THRESHOLD 0.1    // Seuil pour la fonction de différence.

#define TAU_MIN             50.0
#define TAU_MAX             500.0


int16_t signal_buffer_16bit[BLOCK_SIZE]; // Buffer pour l'analyse YIN


// --- Configuration Neopixel (non modifié) ---
const uint16_t PixelCount = 1;
const uint8_t PixelPin = 22;
Adafruit_NeoPixel pixels(PixelCount, PixelPin, NEO_GRB + NEO_KHZ800);
// ---------------------------------------------

// Implémentation complète de l'algorithme YIN
float Yin_detect_pitch(int16_t* audioBuffer, uint16_t bufferSize, uint32_t sampleRate) {
    uint16_t tau_max = bufferSize / 2; //on met la valeur max du retard (tau) à la taille du buff/2 car on souhaite avoir au moins 2 periodes de la note
    float diff[tau_max]; 
    float cumulativeSum = 0.0;
    
    // 1. Fonction de Différence (d(tau))
    for (uint16_t tau = 0; tau < tau_max; tau++) { //on boucle sur toute les valeur possible du retard
        diff[tau] = 0.0;
        for (uint16_t j = 0; j < bufferSize - tau; j++) {
            float delta = (float)audioBuffer[j] - audioBuffer[j + tau];
            diff[tau] += delta * delta;
        }
    }

    // 2. Fonction de Différence Cumulée Normalisée (d'(tau))
    diff[0] = 1.0; //car d’(0) = 1
    
    for (uint16_t tau = 1; tau < tau_max; tau++) {
        cumulativeSum += diff[tau]; 
        
        if (cumulativeSum > 0) {
            diff[tau] = diff[tau] * tau / cumulativeSum;
        } else {
            diff[tau] = 1.0; 
        }
    }
    
    // 3. Recherche du Minimum Absolu (Période)
    const uint16_t min_tau = TAU_MIN;
    const uint16_t max_tau = TAU_MAX;
    
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

// fonction de capture d'un bloc de signal via ADC (GBF) ***
void captureSignalBlock(int16_t* buffer, uint16_t size, uint32_t sampleRate) {
    uint32_t period_us = 1000000UL / sampleRate; // UL unsigned long
    uint32_t next_time = micros();

    for (uint16_t i = 0; i < size; i++) {
        // Attendre le bon instant d'échantillonnage
        while ((int32_t)(micros() - next_time) < 0) {
            // attente active
        }
        next_time += period_us;

        // Lire la valeur brute ADC (0..4095 pour ESP32 en 12 bits)
        int raw = analogRead(ADC_PIN);

        // Convertir en 16 bits signé centré autour de 0
        // -2048..+2047 (puis étendu sur 16 bits)
        int16_t centered = (int16_t)(raw - 2048);
        buffer[i] = centered;
    }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Accordeur YIN I2S ---");

  // Initialisation Neopixel
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 50)); // Bleu doux : Prêt
  pixels.show();

  analogReadResolution(12);      // Résolution ADC ESP32 (0..4095)
  //analogSetPinAttenuation(ADC_PIN, ADC_11db); // pour accepter ~0–3.3 V

  Serial.print("Lecture sur ADC_PIN = ");
  Serial.println(ADC_PIN);
  Serial.print("Sample rate = ");
  Serial.print(SAMPLE_RATE);
  Serial.println(" Hz");
}


void loop() {
  
    // Capture d'un bloc d'échantillons du signal GBF
    captureSignalBlock(signal_buffer_16bit, BLOCK_SIZE, SAMPLE_RATE);
    
    // 3. Détection de la fréquence fondamentale (F0) avec YIN
    float frequency = Yin_detect_pitch(signal_buffer_16bit, BLOCK_SIZE, SAMPLE_RATE);

    // 4. Affichage du résultat et logique d'accordage
    if (frequency > 50.0 && frequency < 1500.0) { 
      // Si une fréquence valide est détectée (typiquement instruments) : 50 pour ignorer les bruits et les fausses détections, 1500 pour éviter de détecter accidentellement des harmoniques de haut niveau 
      Serial.print("Frequence detectee: ");
      Serial.print(frequency, 2); // Afficher 2 décimales
      Serial.println(" Hz");
      pixels.setPixelColor(0, pixels.Color(0, 50, 0)); // Vert 
      pixels.show();

    } else {
      // Pas de son ou bruit, fréquence non détectée
      //Serial.println("...");
      pixels.setPixelColor(0, pixels.Color(50, 0, 0)); // Rouge
      pixels.show();
    }
  
}
