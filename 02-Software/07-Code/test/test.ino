#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include "Adafruit_MAX1704X.h"

extern "C" {
  #include "driver/adc.h"
  #include "esp_adc/adc_continuous.h"
}

// ============================================================
// DEBUG
// ============================================================
#define DEBUG_SERIAL 1
#define ENABLE_BATTERY_SIM 1

#if DEBUG_SERIAL
  #define DBG_BEGIN(x)    Serial.begin(x)
  #define DBG_PRINT(...)  Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_BEGIN(x)
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
  #define DBG_PRINTF(...)
#endif

// ============================================================
// PINS (mis à jour)
// ============================================================
// ADC analogique
static const int ADC_PIN = 35;                    // GPIO35 = ADC1_CH7
static const adc_channel_t ADC_CH = ADC_CHANNEL_7;

// I2C DRV2605L + MAX17048
static const int I2C_SDA = 18;
static const int I2C_SCL = 19;
static const uint32_t I2C_FREQ = 50000;

// Haptique PWM -> IN DRV2605L
static const int HAP_PWM_PIN  = 4;
static const int HAP_PWM_FREQ = 1000;
static const int HAP_PWM_RES  = 8;

// LEDs / bouton
static const int LED_BATT_R = 27;
static const int LED_BATT_G = 12;
static const int LED_BATT_B = 14;
static const int LED_INST_R = 32;
static const int LED_INST_G = 13;
static const int LED_INST_B = 33;
static const int BTN_PIN    = 34;

// LED PWM
static const int LED_PWM_FREQ = 5000;
static const int LED_PWM_RES  = 8;

// ============================================================
// ADC continuous (DMA) - "ADC classique" stable
// ============================================================
static const uint32_t SAMPLE_RATE_HZ = 20000;
static const float    SAMPLE_RATE    = 20000.0f;
static const uint16_t YIN_BUFFER_SIZE = 1024;

adc_continuous_handle_t adc_handle = NULL;
static const uint32_t ADC_READ_LEN = 1024;
uint8_t adc_dma_buffer[ADC_READ_LEN];

// ring buffer ADC -> audio task
volatile uint16_t g_ring[YIN_BUFFER_SIZE];
volatile uint16_t g_ringWrite = 0;
volatile uint16_t g_totalSamples = 0;
volatile bool     g_ringPrimed = false;

static portMUX_TYPE gRingMux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// Instruments
// ============================================================
enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
volatile Instrument profilActuel = GUITARE;

struct Config {
  float fMin;
  float fMax;
  const char* nom;
  float yinGood;   // seuil YIN par profil
};

static Config reglages[] = {
  {190.0f, 700.0f,  "Violon",      0.20f},
  {80.0f,  700.0f,  "Guitare",     0.20f},
  {260.0f, 700.0f,  "Ukulele",     0.20f},
  {30.0f,  200.0f,  "Basse",       0.26f},
  {30.0f,  2000.0f, "Chromatique", 0.20f}
};

// ============================================================
// I2C mutex
// ============================================================
static SemaphoreHandle_t g_i2cMutex = nullptr;
static inline void i2cLock()   { if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
static inline void i2cUnlock() { if (g_i2cMutex) xSemaphoreGive(g_i2cMutex); }

// ============================================================
// DRV2605L (pilotage moteur conservé)
// ============================================================
static const uint8_t DRV_ADDR = 0x5A;
static const uint8_t REG_MODE = 0x01; // bit6 = STANDBY

static volatile bool     g_drvOk = false;
static volatile uint32_t g_lastPitchMs = 0;
static const uint32_t    PITCH_TIMEOUT_MS = 250;

// mute DSP après vibration
static volatile uint32_t g_hapticMuteUntilMs = 0;
static const uint32_t HAPTIC_DSP_MUTE_MS = 100;

// anti burst I2C standby
static uint32_t g_lastHapticIoMs = 0;
static bool g_drvStandbyState = true;

// loi haptique
static const float HAP_DEADBAND_CENTS = 5.0f;
static const float HAP_MAXCENTS       = 50.0f;

static bool drvWriteReg(uint8_t reg, uint8_t val) {
  for (int attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(DRV_ADDR);
    Wire.write(reg);
    Wire.write(val);
    if (Wire.endTransmission(true) == 0) return true;
    delay(5);
  }
  return false;
}

static uint8_t drvReadReg(uint8_t reg) {
  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(DRV_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

static bool drvStandby(bool en) {
  uint8_t m = drvReadReg(REG_MODE);
  if (m == 0xFF) return false;
  if (en) m |=  (1 << 6);
  else    m &= ~(1 << 6);
  return drvWriteReg(REG_MODE, m);
}

static bool drvInitPwmInput() {
  if (!drvWriteReg(0x01, 0x80)) return false; // reset
  delay(10);

  if (!drvWriteReg(0x01, 0x03)) return false; // PWM/Analog input mode
  delay(5);

  uint8_t c1 = drvReadReg(0x1A);
  if (c1 == 0xFF) return false;
  c1 &= (uint8_t)~(1 << 7); // ERM
  if (!drvWriteReg(0x1A, c1)) return false;

  uint8_t c3 = drvReadReg(0x1D);
  if (c3 == 0xFF) return false;
  c3 |=  (uint8_t)(1 << 5); // ERM open-loop
  c3 &= (uint8_t)~(1 << 1); // PWM input
  if (!drvWriteReg(0x1D, c3)) return false;

  if (!drvWriteReg(0x17, 0x8C)) return false; // OD_CLAMP
  return true;
}

static void drvDump() {
  uint8_t r01 = drvReadReg(0x01);
  uint8_t r1a = drvReadReg(0x1A);
  uint8_t r1d = drvReadReg(0x1D);
  DBG_PRINTF("DRV regs: MODE=0x%02X FB=0x%02X CTL3=0x%02X\n", r01, r1a, r1d);
}

// ============================================================
// Battery (MAX17048 + simulation)
// ============================================================
Adafruit_MAX17048 maxlipo;
static bool g_battOk = false;

static const float ORANGE_PCT = 30.0f;
static const float RED_PCT    = 10.0f;
static const uint32_t BATT_PERIOD_MS = 60000;

#if ENABLE_BATTERY_SIM
volatile bool  g_battSimMode = false;
volatile float g_battSimSoc  = 50.0f;
volatile float g_battSimVbat = 3.90f;
volatile bool  g_battForceRefresh = false;
#endif

// ============================================================
// LED helpers
// ============================================================
static inline void pwmAttach(int pin, int freq, int res) {
  ledcAttach(pin, freq, res);    // API Arduino ESP32 v3.x
  ledcWrite(pin, 0);
}

static inline void setBattRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LED_BATT_R, r);
  ledcWrite(LED_BATT_G, g);
  ledcWrite(LED_BATT_B, b);
}
static inline void setInstRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LED_INST_R, r);
  ledcWrite(LED_INST_G, g);
  ledcWrite(LED_INST_B, b);
}
static inline void setBatteryColor(float soc) {
  if (!g_battOk || isnan(soc)) { setBattRGB(255, 0, 255); return; }
  if (soc <= RED_PCT)          { setBattRGB(255, 0, 0);   return; }
  if (soc <= ORANGE_PCT)       { setBattRGB(255, 80, 0);  return; }
  setBattRGB(0, 255, 0);
}
static inline void setInstrumentColor(Instrument p) {
  switch (p) {
    case GUITARE:     setInstRGB(0, 0, 255);   break;
    case UKULELE:     setInstRGB(0, 255, 255); break;
    case BASSE:       setInstRGB(255, 0, 0);   break;
    case VIOLON:      setInstRGB(255, 0, 255); break;
    case CHROMATIQUE: setInstRGB(0, 255, 0);   break;
  }
}

// ============================================================
// DSP buffers / filtres
// ============================================================
static float g_audioBuffer[YIN_BUFFER_SIZE];
static float g_yinBuffer[YIN_BUFFER_SIZE];
static float g_hann[YIN_BUFFER_SIZE];
static bool  g_hannInit = false;

static float g_hpAlpha, g_lpAlpha;
static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;

// gate adaptatif
static const float SILENCE_RMS_THRES = 0.0012f;
static float g_noiseFloor = 0.0f;
static float g_gateRms    = SILENCE_RMS_THRES;
static bool  g_noiseEmaInit = false;
static float g_noiseEma = 0.0f;
static const float NOISE_EMA_ALPHA = 0.02f;

// partage
static volatile float g_lastFreq  = -1.0f;
static volatile float g_lastCents = 0.0f;
static volatile float g_lastRms   = 0.0f;
static volatile float g_lastYin   = 1.0f;
static volatile bool  g_hasPitch  = false;

// cents smoothing
static float g_centsSmooth = 0.0f;
static bool  g_centsInit   = false;

// pitch tracking robustesse
static float   g_candidateFreq = 0.0f;
static uint8_t g_candidateCount = 0;
static uint32_t g_lastValidDetectMs = 0;
static float   g_lastAcceptedFreq = 0.0f;

static float g_fPrev1 = 0.0f, g_fPrev2 = 0.0f;
static bool  g_havePrev1 = false, g_havePrev2 = false;
static float g_fEma = 0.0f;
static bool  g_fEmaInit = false;
static const float FREQ_EMA_ALPHA = 0.35f;

// rejet secteur 50Hz
static const float MAINS_REJECT_FMIN = 47.0f;
static const float MAINS_REJECT_FMAX = 53.0f;
static const float MAINS_REJECT_MAX_EXCESS = 0.0030f;

// correction octave guitare
static const float guitarStringsHz[] = {
  82.4069f, 110.000f, 146.832f, 195.998f, 246.942f, 329.628f
};

// note dominante
static const int NOTE_WINDOW = 5;
static float g_freqWin[NOTE_WINDOW];
static int   g_midiWin[NOTE_WINDOW];
static int   g_winIdx = 0;
static int   g_winCount = 0;
static int   g_lockedMidi = -1;

// stats
static uint32_t g_statRejectYin   = 0;
static uint32_t g_statRejectMains = 0;
static uint32_t g_statRejectImpl  = 0;
static uint32_t g_statFramesOK    = 0;
static uint32_t g_lastStatsMs     = 0;

// ============================================================
// Utils
// ============================================================
static void initHannWindow() {
  for (int i = 0; i < YIN_BUFFER_SIZE; i++) {
    g_hann[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float)i / (float)(YIN_BUFFER_SIZE - 1));
  }
  g_hannInit = true;
}

static inline float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

static void resetPitchTrackingQuick() {
  g_candidateFreq = 0.0f;
  g_candidateCount = 0;
  g_lastValidDetectMs = 0;
}

static void resetFreqSmoother() {
  g_havePrev1 = g_havePrev2 = false;
  g_fEmaInit = false;
}

static void resetDetectionState() {
  resetPitchTrackingQuick();
  resetFreqSmoother();
  g_lastAcceptedFreq = 0.0f;
  g_centsInit = false;
  g_hasPitch = false;
  g_winCount = 0;
  g_lockedMidi = -1;
}

static bool isCloseFreq(float a, float b, float relTol = 0.04f) {
  if (a <= 0.0f || b <= 0.0f) return false;
  float r = a / b;
  if (r < 1.0f) r = 1.0f / r;
  return r <= (1.0f + relTol);
}

static bool isFrequencyPlausible(float f, Instrument p) {
  if (g_lastAcceptedFreq <= 0.0f) return true;
  float ratio = f / g_lastAcceptedFreq;
  if (ratio < 1.0f) ratio = 1.0f / ratio;
  if (p == GUITARE) return ratio <= 2.2f;
  return ratio <= 1.6f;
}

static float smoothFrequencyRobust(float fRaw) {
  float fMed = fRaw;
  if (!g_havePrev1) {
    g_fPrev1 = fRaw; g_havePrev1 = true;
  } else if (!g_havePrev2) {
    g_fPrev2 = g_fPrev1; g_fPrev1 = fRaw; g_havePrev2 = true;
  } else {
    fMed = median3(fRaw, g_fPrev1, g_fPrev2);
    g_fPrev2 = g_fPrev1;
    g_fPrev1 = fRaw;
  }

  if (!g_fEmaInit) {
    g_fEma = fMed;
    g_fEmaInit = true;
  } else {
    g_fEma = (1.0f - FREQ_EMA_ALPHA) * g_fEma + FREQ_EMA_ALPHA * fMed;
  }
  return g_fEma;
}

static float centsToRef(float f, float ref) {
  return 1200.0f * log2f(f / ref);
}

static float bestAbsCentsToGuitarStrings(float f) {
  float best = 1e9f;
  for (float ref : guitarStringsHz) {
    float c = fabsf(centsToRef(f, ref));
    if (c < best) best = c;
  }
  return best;
}

static float correctOctaveForGuitar(float f) {
  if (profilActuel != GUITARE) return f;

  float f1 = f;
  float f2 = f * 2.0f;

  bool f1ok = (f1 >= 80.0f && f1 <= 700.0f);
  bool f2ok = (f2 >= 80.0f && f2 <= 700.0f);

  float c1 = f1ok ? bestAbsCentsToGuitarStrings(f1) : 1e9f;
  float c2 = f2ok ? bestAbsCentsToGuitarStrings(f2) : 1e9f;

  if (c2 + 8.0f < c1) return f2;
  return f1;
}

static float computeRMS(const float *buffer, size_t length) {
  double sumSq = 0;
  for (size_t i = 0; i < length; i++) sumSq += (double)buffer[i] * buffer[i];
  return (float)sqrt(sumSq / length);
}

static bool computeAdaptiveGate(float rms, float &gateUsed) {
  if (!g_noiseEmaInit) {
    g_noiseEma = g_gateRms;
    g_noiseEmaInit = true;
  } else {
    float silenceLimit = g_gateRms * 0.85f;
    if (rms < silenceLimit) {
      g_noiseEma = (1.0f - NOISE_EMA_ALPHA) * g_noiseEma + NOISE_EMA_ALPHA * rms;
    }
  }

  float gate = g_noiseEma * 2.5f;
  if (gate < SILENCE_RMS_THRES) gate = SILENCE_RMS_THRES;
  if (gate > 0.02f) gate = 0.02f;

  gateUsed = gate;
  return (rms >= gate);
}

// ============================================================
// ADC continuous init/read
// ============================================================
static inline bool decodeAdcSample(const uint8_t* p, uint16_t &outRaw12) {
  const adc_digi_output_data_t* d = (const adc_digi_output_data_t*)p;
  if (d->type1.channel != ADC_CH) return false;
  outRaw12 = d->type1.data;
  return true;
}

static bool initContinuousADC(uint32_t sampleRateHz) {
  adc_continuous_handle_cfg_t adc_config = {};
  adc_config.max_store_buf_size = 8192;
  adc_config.conv_frame_size    = ADC_READ_LEN;

  esp_err_t err = adc_continuous_new_handle(&adc_config, &adc_handle);
  if (err != ESP_OK) {
    DBG_PRINTF("adc_continuous_new_handle failed: %d\n", err);
    return false;
  }

  adc_digi_pattern_config_t pattern = {};
  pattern.atten = ADC_ATTEN_DB_6;
  pattern.channel = ADC_CH;
  pattern.unit = ADC_UNIT_1;
  pattern.bit_width = ADC_BITWIDTH_12;

  adc_continuous_config_t dig_cfg = {};
  dig_cfg.sample_freq_hz = sampleRateHz;
  dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
  dig_cfg.pattern_num = 1;
  dig_cfg.adc_pattern = &pattern;

  err = adc_continuous_config(adc_handle, &dig_cfg);
  if (err != ESP_OK) {
    DBG_PRINTF("adc_continuous_config failed: %d\n", err);
    return false;
  }

  err = adc_continuous_start(adc_handle);
  if (err != ESP_OK) {
    DBG_PRINTF("adc_continuous_start failed: %d\n", err);
    return false;
  }
  return true;
}

static void pushAdcSampleRing(uint16_t s) {
  taskENTER_CRITICAL(&gRingMux);
  g_ring[g_ringWrite] = s;
  g_ringWrite = (g_ringWrite + 1) % YIN_BUFFER_SIZE;

  if (!g_ringPrimed) {
    if (g_totalSamples < YIN_BUFFER_SIZE) g_totalSamples++;
    if (g_totalSamples >= YIN_BUFFER_SIZE) g_ringPrimed = true;
  }
  taskEXIT_CRITICAL(&gRingMux);
}

static bool readAdcBlock(float *buffer, size_t n) {
  size_t got = 0;

  while (got < n) {
    uint32_t bytesRead = 0;
    esp_err_t err = adc_continuous_read(adc_handle, adc_dma_buffer, ADC_READ_LEN, &bytesRead, 10);

    if (err != ESP_OK || bytesRead == 0) {
      // fallback rapide: essaie de prendre depuis le ring si déjà primé
      taskENTER_CRITICAL(&gRingMux);
      bool primed = g_ringPrimed;
      uint16_t start = g_ringWrite;
      taskEXIT_CRITICAL(&gRingMux);

      if (!primed) continue;

      for (size_t i = 0; i < n; i++) {
        uint16_t raw;
        taskENTER_CRITICAL(&gRingMux);
        raw = g_ring[(start + i) % YIN_BUFFER_SIZE];
        taskEXIT_CRITICAL(&gRingMux);

        float x = ((float)raw - 2048.0f) / 2048.0f;
        buffer[i] = x;
      }
      return true;
    }

    const uint32_t sampleSize = SOC_ADC_DIGI_RESULT_BYTES;
    for (uint32_t i = 0; i + sampleSize <= bytesRead && got < n; i += sampleSize) {
      uint16_t raw12;
      if (!decodeAdcSample(&adc_dma_buffer[i], raw12)) continue;

      pushAdcSampleRing(raw12); // garde aussi un historique
      float x = ((float)raw12 - 2048.0f) / 2048.0f;
      buffer[got++] = x;
    }
  }

  return true;
}

// ============================================================
// Haptique helpers
// ============================================================
static void hapticsHardOff() {
  ledcWrite(HAP_PWM_PIN, 0);

  uint32_t now = millis();
  if (g_drvOk && !g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
    i2cLock();
    bool ok = drvStandby(true);
    i2cUnlock();
    if (ok) {
      g_drvStandbyState = true;
      g_lastHapticIoMs = now;
    }
  }
}

static void hapticsOn(uint8_t duty) {
  if (!g_drvOk) {
    ledcWrite(HAP_PWM_PIN, 0);
    return;
  }

  uint32_t now = millis();

  if (g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
    i2cLock();
    bool ok = drvStandby(false);
    i2cUnlock();
    if (ok) {
      g_drvStandbyState = false;
      g_lastHapticIoMs = now;
    }
  }

  ledcWrite(HAP_PWM_PIN, duty);
  g_hapticMuteUntilMs = now + HAPTIC_DSP_MUTE_MS;
}

// duty linéaire (comme tu voulais)
static void updateHapticsFromCents(float cents) {
  if (!g_hasPitch || (millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) {
    hapticsHardOff();
    return;
  }

  float a = fabsf(cents);

  static bool vibOn = false;
  const float ON_TH  = 5.5f;
  const float OFF_TH = 4.0f;
  if (!vibOn) vibOn = (a >= ON_TH);
  else        vibOn = (a >= OFF_TH);

  if (!vibOn) {
    hapticsHardOff();
    return;
  }

  if (a > HAP_MAXCENTS) a = HAP_MAXCENTS;

  // mapping linéaire
  const uint8_t DUTY_MIN = 45;   // augmente un peu si moteur ne démarre pas
  const uint8_t DUTY_MAX = 220;

  float x;
  if (a <= HAP_DEADBAND_CENTS) x = 0.0f;
  else x = (a - HAP_DEADBAND_CENTS) / (HAP_MAXCENTS - HAP_DEADBAND_CENTS);

  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;

  uint8_t duty = (uint8_t)(DUTY_MIN + x * (float)(DUTY_MAX - DUTY_MIN));

  // motif temporel directionnel (gardé)
  const uint32_t periodMs = 100;
  uint32_t t = millis() % periodMs;

  uint32_t onBase  = (cents > 0) ? 22 : 50; // trop haut = impulsions plus courtes
  uint32_t onBonus = (uint32_t)(x * 20.0f);
  uint32_t onMs    = onBase + onBonus;
  if (onMs > 80) onMs = 80;

  if (t < onMs) hapticsOn(duty);
  else          hapticsHardOff();
}

// ============================================================
// Notes / YIN
// ============================================================
static void changerProfil(Instrument p) {
  profilActuel = p;
  Config c = reglages[p];

  float fHP = c.fMin * 0.8f;
  float fLP = c.fMax * 1.2f;

  g_hpAlpha = expf(-2.0f * 3.1415926f * fHP / SAMPLE_RATE);
  g_lpAlpha = expf(-2.0f * 3.1415926f * fLP / SAMPLE_RATE);

  g_hpY = 0; g_hpXprev = 0; g_lpY = 0;
  g_noiseEmaInit = false;
  g_hapticMuteUntilMs = 0;

  for (int i = 0; i < NOTE_WINDOW; i++) { g_freqWin[i] = 0; g_midiWin[i] = -9999; }
  g_winIdx = 0; g_winCount = 0; g_lockedMidi = -1;

  resetDetectionState();
  setInstrumentColor(p);

  DBG_PRINTF("\n>>> Profil: %s (%.0fHz - %.0fHz)\n", c.nom, c.fMin, c.fMax);
}

static void appliquerFiltre(float *buffer, int taille) {
  if (!g_hannInit) initHannWindow();

  for (int i = 0; i < taille; i++) {
    float x = buffer[i];

    float yhp = g_hpAlpha * (g_hpY + x - g_hpXprev);
    g_hpY = yhp; g_hpXprev = x;

    float ylp = g_lpAlpha * g_lpY + (1.0f - g_lpAlpha) * yhp;
    g_lpY = ylp;

    buffer[i] = ylp * g_hann[i];
  }
}

static float detectPitchYIN(const float *input, float *outYinMin = nullptr) {
  int tauMin = (int)(SAMPLE_RATE / reglages[profilActuel].fMax);
  int tauMax = (int)(SAMPLE_RATE / reglages[profilActuel].fMin);
  if (tauMax >= YIN_BUFFER_SIZE - 2) tauMax = YIN_BUFFER_SIZE - 2;
  if (tauMin < 2) tauMin = 2;

  for (int tau = 0; tau <= tauMax; tau++) {
    double sum = 0;
    for (int i = 0; i < YIN_BUFFER_SIZE - tau; i++) {
      float d = input[i] - input[i + tau];
      sum += (double)d * (double)d;
    }
    g_yinBuffer[tau] = (float)sum;
  }

  g_yinBuffer[0] = 1.0f;
  double runningSum = 0;
  float yinMin = 1.0f;

  for (int tau = 1; tau <= tauMax; tau++) {
    runningSum += g_yinBuffer[tau];
    g_yinBuffer[tau] = (float)(g_yinBuffer[tau] * tau / (float)runningSum);
    if (tau >= tauMin && g_yinBuffer[tau] < yinMin) yinMin = g_yinBuffer[tau];
  }

  const float threshold = 0.15f;
  int tauFound = -1;
  for (int tau = tauMin; tau <= tauMax; tau++) {
    if (g_yinBuffer[tau] < threshold) {
      while (tau + 1 <= tauMax && g_yinBuffer[tau + 1] < g_yinBuffer[tau]) tau++;
      tauFound = tau;
      break;
    }
  }

  if (outYinMin) *outYinMin = yinMin;
  if (tauFound < 0) return -1.0f;

  float s0 = g_yinBuffer[tauFound - 1];
  float s1 = g_yinBuffer[tauFound];
  float s2 = g_yinBuffer[tauFound + 1];

  float denom = (2.0f * s1 - s2 - s0);
  float delta = 0.0f;
  if (fabsf(denom) > 1e-9f) delta = 0.5f * (s2 - s0) / denom;

  float tauInterp = tauFound + delta;

  // Heuristique harmonique
  for (int k = 2; k <= 4; k++) {
    int t = (int)(tauInterp / k);
    if (t >= tauMin && g_yinBuffer[t] < 0.20f) tauInterp /= k;
  }

  if (tauInterp <= 0.0f) return -1.0f;
  return SAMPLE_RATE / tauInterp;
}

static int freqToMidi(float freq) {
  float n = 12.0f * log2f(freq / 440.0f);
  return (int)roundf(n) + 69;
}

static float midiToFreq(int midi) {
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

static void midiToNoteName(int midi, const char **note, int *oct) {
  static const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int idx = ((midi % 12) + 12) % 12;
  *note = names[idx];
  *oct = midi / 12 - 1;
}

static float centsAgainstMidi(float freq, int midiRef) {
  float ref = midiToFreq(midiRef);
  return 1200.0f * log2f(freq / ref);
}

static int dominantMidi(const int *midiArr, int n, int *outCount) {
  int bestMidi = midiArr[0];
  int bestCount = 1;
  for (int i = 0; i < n; i++) {
    int m = midiArr[i];
    int count = 0;
    for (int j = 0; j < n; j++) if (midiArr[j] == m) count++;
    if (count > bestCount) { bestCount = count; bestMidi = m; }
  }
  if (outCount) *outCount = bestCount;
  return bestMidi;
}

static float medianFloat(float *arr, int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (arr[j] < arr[i]) { float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp; }
    }
  }
  if (n <= 0) return 0.0f;
  if (n % 2 == 1) return arr[n / 2];
  return 0.5f * (arr[n/2 - 1] + arr[n/2]);
}

// ============================================================
// Battery helpers + simulation commandes
// ============================================================
static bool readBatterySafe(float &vbat, float &soc) {
  for (int attempt = 0; attempt < 5; attempt++) {
    vbat = maxlipo.cellVoltage();
    soc  = maxlipo.cellPercent();
    bool okV = !isnan(vbat) && (vbat > 2.5f) && (vbat < 4.5f);
    bool okS = !isnan(soc)  && (soc >= 0.0f) && (soc <= 100.0f);
    if (okV && okS) return true;
    delay(60);
  }
  return false;
}

#if ENABLE_BATTERY_SIM
static void requestBatteryRefresh() {
  g_battForceRefresh = true;
}

static void printBatteryHelp() {
  DBG_PRINTLN("\n[BAT] Commandes:");
  DBG_PRINTLN("  bat help");
  DBG_PRINTLN("  bat sim");
  DBG_PRINTLN("  bat real");
  DBG_PRINTLN("  bat soc <0-100>");
  DBG_PRINTLN("  bat vbat <2.5-4.5>");
  DBG_PRINTLN("  bat show");
}

static void handleBatterySerialCommands() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  String lower = line;
  lower.toLowerCase();
  if (!lower.startsWith("bat")) return;

  if (lower == "bat" || lower == "bat help") {
    printBatteryHelp();
    return;
  }
  if (lower == "bat sim") {
    g_battSimMode = true;
    requestBatteryRefresh();
    DBG_PRINTLN("[BAT] Mode simulation actif");
    return;
  }
  if (lower == "bat real") {
    g_battSimMode = false;
    requestBatteryRefresh();
    DBG_PRINTLN("[BAT] Mode reel actif");
    return;
  }
  if (lower.startsWith("bat soc ")) {
    float v = lower.substring(8).toFloat();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_battSimMode = true;
    g_battSimSoc = v;
    requestBatteryRefresh();
    DBG_PRINTF("[BAT] SOC simule = %.1f%%\n", v);
    return;
  }
  if (lower.startsWith("bat vbat ")) {
    float v = lower.substring(9).toFloat();
    if (v < 2.5f) v = 2.5f;
    if (v > 4.5f) v = 4.5f;
    g_battSimMode = true;
    g_battSimVbat = v;
    requestBatteryRefresh();
    DBG_PRINTF("[BAT] VBAT simulee = %.3fV\n", v);
    return;
  }
  if (lower == "bat show") {
    DBG_PRINTF("[BAT] Mode=%s | VBAT=%.3fV | SOC=%.1f%%\n",
               g_battSimMode ? "SIM" : "REAL",
               g_battSimMode ? g_battSimVbat : NAN,
               g_battSimMode ? g_battSimSoc  : NAN);
    return;
  }

  DBG_PRINTLN("[BAT] Commande inconnue");
}
#endif

// ============================================================
// Calibrage bruit (boot)
// ============================================================
static void calibrateNoiseFloor() {
  const int frames = 20;
  double acc = 0.0;

  hapticsHardOff();

  for (int i = 0; i < frames; i++) {
    if (!readAdcBlock(g_audioBuffer, YIN_BUFFER_SIZE)) continue;
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    acc += rms;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  g_noiseFloor = (float)(acc / (double)frames);
  g_gateRms = fmaxf(SILENCE_RMS_THRES, g_noiseFloor * 2.0f); // un peu plus sensible

  g_noiseEma = g_gateRms;
  g_noiseEmaInit = true;

  DBG_PRINTF("NoiseFloor=%.6f -> GateBase=%.6f\n", g_noiseFloor, g_gateRms);
}

// ============================================================
// TASKS
// ============================================================
static void TaskBattery(void *pv) {
  (void)pv;

  uint32_t nextRead = millis();

  for (;;) {
    bool forceNow = false;
#if ENABLE_BATTERY_SIM
    if (g_battForceRefresh) { g_battForceRefresh = false; forceNow = true; }
#endif

    uint32_t now = millis();
    if ((int32_t)(now - nextRead) >= 0 || forceNow) {
      float v = NAN, s = NAN;
      bool ok = false;

#if ENABLE_BATTERY_SIM
      if (g_battSimMode) {
        v = g_battSimVbat;
        s = g_battSimSoc;
        ok = true;
      } else
#endif
      if (g_battOk) {
        i2cLock();
        ok = readBatterySafe(v, s);
        i2cUnlock();
      }

      if (ok) {
        setBatteryColor(s);
        DBG_PRINTF("[BAT] %s VBAT=%.3fV SOC=%.1f%%\n",
#if ENABLE_BATTERY_SIM
                   g_battSimMode ? "(SIM)" : "(REAL)",
#else
                   "(REAL)",
#endif
                   v, s);
      } else {
        setBatteryColor(NAN);
        DBG_PRINTLN("[BAT] lecture echec");
      }

      nextRead = millis() + BATT_PERIOD_MS;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static void TaskUI(void *pv) {
  (void)pv;
  int last = digitalRead(BTN_PIN);
  uint32_t lastChange = millis();

  for (;;) {
    int now = digitalRead(BTN_PIN);
    if (now != last) { last = now; lastChange = millis(); }

    if ((millis() - lastChange) > 30) {
      if (now == LOW) {
        Instrument p = (Instrument)(((int)profilActuel + 1) % 5);
        changerProfil(p);
        while (digitalRead(BTN_PIN) == LOW) vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void TaskAudio(void *pv) {
  (void)pv;
  static bool gateOpen = false;

  const float ALPHA = 0.12f;
  const float MAX_STEP_CENTS = 2.0f;

  for (;;) {
    if (!readAdcBlock(g_audioBuffer, YIN_BUFFER_SIZE)) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);

    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    g_lastRms = rms;

    float gateUsed = g_gateRms;
    bool hasSignal = computeAdaptiveGate(rms, gateUsed);

    const float gateOn  = gateUsed;
    const float gateOff = gateUsed * 0.70f;

    if (!gateOpen) gateOpen = (rms >= gateOn);
    else           gateOpen = (rms >= gateOff);

    if (!gateOpen) {
      resetDetectionState();
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }

    if (millis() < (uint32_t)g_hapticMuteUntilMs) {
      resetPitchTrackingQuick();
      g_hasPitch = false;
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    if (g_lastValidDetectMs != 0 && (millis() - g_lastValidDetectMs) > 350) {
      resetPitchTrackingQuick();
      resetFreqSmoother();
      g_lastAcceptedFreq = 0.0f;
    }

    float yinMin = 1.0f;
    float f_micro = detectPitchYIN(g_audioBuffer, &yinMin);
    g_lastYin = yinMin;

    const float YIN_GOOD = reglages[profilActuel].yinGood;
    bool pitchValid = (f_micro > 0.0f) && (yinMin > 0.0f) && (yinMin <= YIN_GOOD);

    if (!pitchValid) {
      g_statRejectYin++;
      g_hasPitch = false;
      if ((millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    // Validation multi-trames (plus rapide pour guitare)
    {
      float relTol = (profilActuel == CHROMATIQUE) ? 0.025f : 0.05f;
      uint8_t confirmNeeded = (profilActuel == CHROMATIQUE) ? 3 : 2;

      if (g_candidateCount == 0) {
        g_candidateFreq = f_micro;
        g_candidateCount = 1;
        g_hasPitch = false;
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }

      if (!isCloseFreq(f_micro, g_candidateFreq, relTol)) {
        g_candidateFreq = f_micro;
        g_candidateCount = 1;
        g_hasPitch = false;
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }

      g_candidateCount++;
      g_candidateFreq = 0.7f * g_candidateFreq + 0.3f * f_micro;

      if (g_candidateCount < confirmNeeded) {
        g_hasPitch = false;
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
    }

    float f = g_candidateFreq;
    g_candidateCount = 0;
    g_lastValidDetectMs = millis();

    if (profilActuel == GUITARE) f = correctOctaveForGuitar(f);

    float rmsExcess = rms - g_noiseEma;
    if (profilActuel == CHROMATIQUE &&
        f >= MAINS_REJECT_FMIN && f <= MAINS_REJECT_FMAX &&
        rmsExcess < MAINS_REJECT_MAX_EXCESS) {
      g_statRejectMains++;
      g_hasPitch = false;
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (!isFrequencyPlausible(f, profilActuel)) {
      g_statRejectImpl++;
      g_hasPitch = false;
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    f = smoothFrequencyRobust(f);

    g_lastAcceptedFreq = f;
    g_lastFreq = f;
    g_hasPitch = true;
    g_lastPitchMs = millis();

    // note dominante + cents médiane
    int midi = freqToMidi(f);
    g_freqWin[g_winIdx] = f;
    g_midiWin[g_winIdx] = midi;
    g_winIdx = (g_winIdx + 1) % NOTE_WINDOW;
    if (g_winCount < NOTE_WINDOW) g_winCount++;

    if (g_winCount < 2) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    int n = g_winCount;
    int midis[NOTE_WINDOW];
    for (int i = 0; i < n; i++) midis[i] = g_midiWin[i];

    int domCount = 0;
    int domMidi = dominantMidi(midis, n, &domCount);

    const float LOCK_RATIO = 0.6f;
    if (g_lockedMidi < 0) g_lockedMidi = domMidi;
    else if (domMidi != g_lockedMidi) {
      if ((float)domCount >= LOCK_RATIO * n) g_lockedMidi = domMidi;
    }

    float centsList[NOTE_WINDOW];
    int cN = 0;
    for (int i = 0; i < n; i++) {
      float ff = g_freqWin[i];
      if (ff <= 0) continue;
      float c = centsAgainstMidi(ff, g_lockedMidi);
      if (fabsf(c) > 80.0f) continue;
      centsList[cN++] = c;
    }
    if (cN < 1) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    float centsMed = medianFloat(centsList, cN);

    if (!g_centsInit) {
      g_centsSmooth = centsMed;
      g_centsInit = true;
    } else {
      float target = (1.0f - ALPHA) * g_centsSmooth + ALPHA * centsMed;
      float delta = target - g_centsSmooth;
      if (delta >  MAX_STEP_CENTS) delta =  MAX_STEP_CENTS;
      if (delta < -MAX_STEP_CENTS) delta = -MAX_STEP_CENTS;
      g_centsSmooth += delta;
    }

    g_lastCents = g_centsSmooth;
    updateHapticsFromCents(g_centsSmooth);

    const char *noteName;
    int oct;
    midiToNoteName(g_lockedMidi, &noteName, &oct);
    float ref = midiToFreq(g_lockedMidi);

    g_statFramesOK++;

    DBG_PRINTF("[%s] f=%.2fHz note=%s%d ref=%.2fHz centsMed=%+.2f centsSm=%+.2f "
               "N=%d dom=%d/%d rms=%.5f gate=%.5f nEma=%.5f yin=%.3f\n",
               reglages[profilActuel].nom, f, noteName, oct, ref, centsMed, g_centsSmooth,
               cN, domCount, n, rms, gateUsed, g_noiseEma, yinMin);

    uint32_t now = millis();
    if (now - g_lastStatsMs > 5000) {
      DBG_PRINTF("STATS ok=%lu rejYIN=%lu rejMains=%lu rejImpl=%lu\n",
                 (unsigned long)g_statFramesOK,
                 (unsigned long)g_statRejectYin,
                 (unsigned long)g_statRejectMains,
                 (unsigned long)g_statRejectImpl);
      g_lastStatsMs = now;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  DBG_BEGIN(115200);
  delay(150);

  if (!g_hannInit) initHannWindow();

  pinMode(HAP_PWM_PIN, OUTPUT);
  digitalWrite(HAP_PWM_PIN, LOW);

  pinMode(BTN_PIN, INPUT);

  pwmAttach(LED_BATT_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_B, LED_PWM_FREQ, LED_PWM_RES);

  pwmAttach(LED_INST_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_B, LED_PWM_FREQ, LED_PWM_RES);

  pwmAttach(HAP_PWM_PIN, HAP_PWM_FREQ, HAP_PWM_RES);
  ledcWrite(HAP_PWM_PIN, 0);

  setBattRGB(255, 0, 0);

  g_i2cMutex = xSemaphoreCreateMutex();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);
  Wire.setTimeOut(2);
  delay(100);

  changerProfil(GUITARE);

  // ADC continu (GPIO35)
  if (!initContinuousADC(SAMPLE_RATE_HZ)) {
    DBG_PRINTLN("ERREUR: initContinuousADC() a echoue");
    while (true) delay(1000);
  }
  DBG_PRINTF("ADC continu OK sur GPIO%d, Fs=%lu Hz\n", ADC_PIN, (unsigned long)SAMPLE_RATE_HZ);

  // Batterie
  float vbat = NAN, soc = NAN;
  bool ok = false;

  i2cLock();
  g_battOk = maxlipo.begin();
  if (g_battOk) {
    maxlipo.wake();
    maxlipo.quickStart();
    delay(400);
    ok = readBatterySafe(vbat, soc);
  }
  i2cUnlock();

  if (!g_battOk || !ok) {
    DBG_PRINTLN("BATTERIE: lecture echec (mode reel). Simulation possible.");
    setBatteryColor(NAN);
  } else {
    DBG_PRINTF("BATTERIE: VBAT=%.3fV SOC=%.1f%%\n", vbat, soc);
    setBatteryColor(soc);
  }

  // DRV2605L
  bool drvOk = false;
  i2cLock();
  drvOk = drvInitPwmInput();
  if (drvOk) {
    drvDump();
    drvOk = drvStandby(true);
  }
  i2cUnlock();

  g_drvOk = drvOk;
  g_drvStandbyState = true;
  DBG_PRINTLN(drvOk ? "DRV2605L: OK + STANDBY" : "DRV2605L: INIT ECHEC");

  hapticsHardOff();

  if (drvOk) calibrateNoiseFloor();

#if ENABLE_BATTERY_SIM
  printBatteryHelp();
#endif

  xTaskCreatePinnedToCore(TaskAudio,   "Audio",   8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(TaskBattery, "Battery", 4096, nullptr, 1, nullptr, 0);

  DBG_PRINTLN("OK: tasks started.");
}

void loop() {
#if ENABLE_BATTERY_SIM
  handleBatterySerialCommands();
#endif
  vTaskDelay(pdMS_TO_TICKS(20));
}
