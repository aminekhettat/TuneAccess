/* YIN Accordeur ESP32 I2S */

// *** 1. Inclure les librairies nécessaires ***
#include "Arduino.h"
#include "driver/i2s.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>


// *** 2. Constantes I2S et YIN (Mises à jour) ***
#define I2S_PORT (i2s_port_t) 0 // Typiquement I2S_NUM_0 (Port 0) est utilisé.
                                 // Nous utiliserons le port 0 car le port 1 est souvent utilisé pour le DAC.
#define SAMPLE_RATE 44100        // Fréquence d'échantillonnage optimisée pour YIN
#define BLOCK_SIZE 1024          // Taille du buffer d'analyse pour YIN
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT //Format d’échantillon I2S configuré à 16 bits
#define YIN_THRESHOLD 0.1    // Seuil pour la fonction de différence.

#define TAU_MIN             50.0
#define TAU_MAX             500.0

#define N_NOTES              12
#define N_OCT                9

// *** 3. Configuration des Broches ***
// Vérifiez que ces broches sont correctes pour votre carte MUSO PROTO !
i2s_pin_config_t pin_config_rx = {
  .bck_io_num = 5, //horloge de bits 
  .ws_io_num = 25, //sélecteur
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

//Table de notes 
struct Row_Note {
  const char* name;       // nom de la note
  float hz[N_OCT];            
};

static const Row_Note Note_Tab[N_NOTES] = {
  {"C",     {16.35, 32.70, 65.41, 130.81, 261.63, 523.25, 1046.50, 2093.00, 4186.01}},
  {"C#/Db", {17.32, 34.65, 69.30, 138.59, 277.18, 554.37, 1108.73, 2217.46, 4434.92}},
  {"D",     {18.35, 36.71, 73.42, 146.83, 293.66, 587.33, 1174.66, 2349.32, 4698.63}},
  {"D#/Eb", {19.45, 38.89, 77.78, 155.56, 311.13, 622.25, 1244.51, 2489.02, 4978.03}},
  {"E",     {20.60, 41.20, 82.41, 164.81, 329.63, 659.26, 1318.51, 2637.02, 5274.04}},
  {"F",     {21.83, 43.65, 87.31, 174.61, 349.23, 698.46, 1396.91, 2793.83, 5587.65}},
  {"F#/Gb", {23.12, 46.25, 92.50, 185.00, 369.99, 739.99, 1479.98, 2959.96, 5919.91}},
  {"G",     {24.50, 49.00, 98.00, 196.00, 392.00, 783.99, 1567.98, 3135.96, 6271.93}},
  {"G#/Ab", {25.96, 51.91, 103.83, 207.65, 415.30, 830.61, 1661.22, 3322.44, 6644.88}},
  {"A",     {27.50, 55.00, 110.00, 220.00, 440.00, 880.00, 1760.00, 3520.00, 7040.00}},
  {"A#/Bb", {29.14, 58.27, 116.54, 233.08, 466.16, 932.33, 1864.66, 3729.31, 7458.62}},
  {"B",     {30.87, 61.74, 123.47, 246.94, 493.88, 987.77, 1975.53, 3951.07, 7902.13}}
};

struct Nearest_Note {
  const char* name;   
  int   octave;       // de 0 à 9
  float f_target;     // Hz de la note la plus proche
  float diffHz;       // f0 - f_target (>0 trop haut, <0 trop bas)
  bool  found;        //si la note à été trouvé
};

Nearest_Note find_Nearest_Note(float f0) {
  Nearest_Note target_note{nullptr, -1, 0.0, 0.0, false};
  if (!(f0 > 0.0)) return target_note;

  float best = 1e6f;                 // initiliser à une grande valeur
  for (int o = 0; o < 9; ++o) {
    for (int n = 0; n < 12; ++n) {
      float ft = Note_Tab[n].hz[o];
      float d  = f0 - ft;                // écart signé
      float ad = d >= 0 ? d : -d;        // |d| pour comparer
      if (ad < best) {
        best        = ad;
        target_note.name      = Note_Tab[n].name;
        target_note.octave    = o;
        target_note.f_target  = ft;
        target_note.diffHz    = d;              // on garde le signe
        target_note.found     = true;
      }
    }
  }
  return target_note;
}

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
  

  Nearest_Note nn = find_Nearest_Note(frequency);
  if (nn.found) {
    Serial.printf("f0 = %.2 Hz  ->  %s%d (%.2 Hz)  diff = %+6.2 Hz\n", frequency, nn.name, nn.octave, nn.f_target, nn.diffHz);
  
    float ad = fabsf(nn.diffHz);           // absolu seulement pour le seuil
    if (ad < 0.5)       pixels.setPixelColor(0, pixels.Color(0, 40, 0));   // vert
    else if (ad < 2.0)  pixels.setPixelColor(0, pixels.Color(40, 20, 0));  // jaune
    else                 pixels.setPixelColor(0, pixels.Color(40, 0, 0));   // rouge
    pixels.show();
  }
