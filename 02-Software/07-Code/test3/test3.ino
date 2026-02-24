//#include <Arduino.h>
//#include "driver/i2s.h"
//#include <Wire.h>
//#include "Adafruit_MAX1704X.h"
//#include <math.h>
//
//// ===============================
////   SERIAL DEBUG
//// ===============================
//#define DEBUG_SERIAL 1
//#define ENABLE_BATTERY_SIM_SERIAL 1
//
//#if DEBUG_SERIAL
//  #define DBG_BEGIN(x)    Serial.begin(x)
//  #define DBG_PRINT(x)    Serial.print(x)
//  #define DBG_PRINTLN(x)  Serial.println(x)
//  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
//#else
//  #define DBG_BEGIN(x)
//  #define DBG_PRINT(x)
//  #define DBG_PRINTLN(x)
//  #define DBG_PRINTF(...)
//#endif
//
//// ===============================
////   PROFILS INSTRUMENTS
//// ===============================
//enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
//volatile Instrument profilActuel = GUITARE;
//
//struct Config {
//  float fMin;
//  float fMax;
//  const char* nom;
//  float yinGood;     // seuil YIN par profil (plus bas = plus strict)
//};
//
//static Config reglages[] = {
//  {190.0f, 700.0f,  "Violon",     0.20f},
//  {80.0f,  700.0f,  "Guitare",    0.20f},
//  {260.0f, 700.0f,  "Ukulele",    0.20f},
//  {30.0f,  200.0f,  "Basse",      0.26f},
//  {30.0f,  2000.0f, "Chromatique",0.20f}
//};
//
//// ===============================
////   I2C
//// ===============================
//static const int I2C_SDA = 21;
//static const int I2C_SCL = 22;
//static const uint32_t I2C_FREQ = 50000;
//
//static SemaphoreHandle_t g_i2cMutex = nullptr;
//static inline void i2cLock()   { if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
//static inline void i2cUnlock() { if (g_i2cMutex) xSemaphoreGive(g_i2cMutex); }
//
//// ===============================
////   DRV2605L
//// ===============================
//static const uint8_t DRV_ADDR = 0x5A;
//
//static const int HAP_PWM_PIN  = 26;
//static const int HAP_PWM_FREQ = 1000;
//static const int HAP_PWM_RES  = 8;
//
//static const uint8_t REG_MODE = 0x01;   // bit6 = STANDBY
//
//static bool drvWriteReg(uint8_t reg, uint8_t val) {
//  for (int attempt = 0; attempt < 3; attempt++) {
//    Wire.beginTransmission(DRV_ADDR);
//    Wire.write(reg);
//    Wire.write(val);
//    if (Wire.endTransmission(true) == 0) return true;
//    delay(5);
//  }
//  return false;
//}
//
//static uint8_t drvReadReg(uint8_t reg) {
//  Wire.beginTransmission(DRV_ADDR);
//  Wire.write(reg);
//  if (Wire.endTransmission(false) != 0) return 0xFF;
//  Wire.requestFrom(DRV_ADDR, (uint8_t)1);
//  return Wire.available() ? Wire.read() : 0xFF;
//}
//
//static bool drvStandby(bool en) {
//  uint8_t m = drvReadReg(REG_MODE);
//  if (m == 0xFF) return false;
//  if (en) m |=  (1 << 6);
//  else    m &= ~(1 << 6);
//  return drvWriteReg(REG_MODE, m);
//}
//
//static bool drvInitPwmInput() {
//  if (!drvWriteReg(0x01, 0x80)) return false; // reset
//  delay(10);
//
//  if (!drvWriteReg(0x01, 0x03)) return false; // MODE=3 PWM/Analog
//  delay(5);
//
//  uint8_t c1 = drvReadReg(0x1A);
//  if (c1 == 0xFF) return false;
//  c1 &= (uint8_t)~(1 << 7); // ERM
//  if (!drvWriteReg(0x1A, c1)) return false;
//
//  uint8_t c3 = drvReadReg(0x1D);
//  if (c3 == 0xFF) return false;
//  c3 |=  (uint8_t)(1 << 5); // ERM_OPEN_LOOP
//  c3 &= (uint8_t)~(1 << 1); // PWM input
//  if (!drvWriteReg(0x1D, c3)) return false;
//
//  if (!drvWriteReg(0x17, 0x8C)) return false; // OD_CLAMP
//  return true;
//}
//
//static void drvDump() {
//  uint8_t r01 = drvReadReg(0x01);
//  uint8_t r1a = drvReadReg(0x1A);
//  uint8_t r1d = drvReadReg(0x1D);
//  DBG_PRINTF("DRV regs: MODE=0x%02X FB=0x%02X CTL3=0x%02X\n", r01, r1a, r1d);
//}
//
//// ===============================
////   BATTERY (MAX17048)
//// ===============================
//Adafruit_MAX17048 maxlipo;
//static bool g_battOk = false;
//
//static const float ORANGE_PCT = 30.0f;
//static const float RED_PCT    = 10.0f;
//static const uint32_t BATT_PERIOD_MS = 60000;
//
//// --- Simulation batterie ---
//#if ENABLE_BATTERY_SIM_SERIAL
//static volatile bool  g_battSimMode = false;      // false=réel, true=simulation
//static volatile float g_battSimSoc  = 50.0f;      // %
//static volatile float g_battSimVbat = 3.90f;      // V
//static volatile bool  g_battForceRefresh = false; // force une lecture immédiate
//#endif
//
//static const int LED_BATT_R = 27;
//static const int LED_BATT_G = 12;
//static const int LED_BATT_B = 14;
//
//// ===============================
////   LED INSTRUMENT
//// ===============================
//static const int LED_INST_R = 32;
//static const int LED_INST_G = 13;
//static const int LED_INST_B = 33;
//
//static const int BTN_PIN = 34;
//
//// ===============================
////   LEDC
//// ===============================
//static const int LED_PWM_FREQ = 5000;
//static const int LED_PWM_RES  = 8;
//
//static inline void pwmAttach(int pin, int freq, int res) {
//  ledcAttach(pin, freq, res);
//  ledcWrite(pin, 0);
//}
//
//// ===============================
////   LED helpers
//// ===============================
//static inline void setBattRGB(uint8_t r, uint8_t g, uint8_t b) {
//  ledcWrite(LED_BATT_R, r);
//  ledcWrite(LED_BATT_G, g);
//  ledcWrite(LED_BATT_B, b);
//}
//static inline void setInstRGB(uint8_t r, uint8_t g, uint8_t b) {
//  ledcWrite(LED_INST_R, r);
//  ledcWrite(LED_INST_G, g);
//  ledcWrite(LED_INST_B, b);
//}
//static inline void setBatteryColor(float soc) {
//  if (!g_battOk && !(
//#if ENABLE_BATTERY_SIM_SERIAL
//      g_battSimMode
//#else
//      false
//#endif
//      )) { setBattRGB(255, 0, 255); return; }
//  if (isnan(soc))              { setBattRGB(255, 0, 255); return; }
//  if (soc <= RED_PCT)          { setBattRGB(255, 0, 0);   return; }
//  if (soc <= ORANGE_PCT)       { setBattRGB(255, 80, 0);  return; }
//  setBattRGB(0, 255, 0);
//}
//static inline void setInstrumentColor(Instrument p) {
//  switch (p) {
//    case GUITARE:     setInstRGB(0, 0, 255);   break;
//    case UKULELE:     setInstRGB(0, 255, 255); break;
//    case BASSE:       setInstRGB(255, 0, 0);   break;
//    case VIOLON:      setInstRGB(255, 0, 255); break;
//    case CHROMATIQUE: setInstRGB(0, 255, 0);   break;
//  }
//}
//
//// ===============================
////   AUDIO I2S
//// ===============================
//static const i2s_port_t I2S_PORT   = I2S_NUM_0;
//static const int I2S_BCK_PIN       = 5;
//static const int I2S_WS_PIN        = 25;
//static const int I2S_DATA_PIN      = 35;
//
//static const float SAMPLE_RATE     = 20000.0f;
//static const int   YIN_BUFFER_SIZE = 1024;
//
//static float g_audioBuffer[YIN_BUFFER_SIZE];
//static float g_yinBuffer[YIN_BUFFER_SIZE];
//
//// Fenêtre de Hann
//static float g_hann[YIN_BUFFER_SIZE];
//static bool  g_hannInit = false;
//
//// filtres HP/LP
//static float g_hpAlpha, g_lpAlpha;
//static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;
//
//// Gate adaptatif dynamique
//static const float SILENCE_RMS_THRES = 0.0012f;
//static float g_noiseFloor = 0.0f;
//static float g_gateRms    = SILENCE_RMS_THRES;
//static bool  g_noiseEmaInit = false;
//static float g_noiseEma     = 0.0f;
//static const float NOISE_EMA_ALPHA = 0.02f;
//
//// ===============================
////   PARTAGE ENTRE TASKS
//// ===============================
//static volatile float g_lastFreq  = -1.0f;
//static volatile float g_lastCents = 0.0f;
//static volatile float g_lastRms   = 0.0f;
//static volatile float g_lastYin   = 1.0f;
//static volatile bool  g_hasPitch  = false;
//
//// ===============================
////   Cents smoothing + slew limiter
//// ===============================
//static float g_centsSmooth = 0.0f;
//static bool  g_centsInit   = false;
//
//// ===============================
////   Pitch tracking robustesse
//// ===============================
//static float   g_candidateFreq = 0.0f;
//static uint8_t g_candidateCount = 0;
//static uint32_t g_lastValidDetectMs = 0;
//static float   g_lastAcceptedFreq = 0.0f;
//
//// Lissage fréquence robuste (médiane3 + EMA)
//static float g_fPrev1 = 0.0f, g_fPrev2 = 0.0f;
//static bool  g_havePrev1 = false, g_havePrev2 = false;
//static float g_fEma = 0.0f;
//static bool  g_fEmaInit = false;
//static const float FREQ_EMA_ALPHA = 0.35f;
//
//// ===============================
////   HAPTICS (produit + hard-off)
//// ===============================
//static volatile bool     g_drvOk = false;
//static volatile uint32_t g_lastPitchMs = 0;
//static const uint32_t    PITCH_TIMEOUT_MS = 250;
//
//static const float HAP_DEADBAND_CENTS = 5.0f;
//static const float HAP_MAXCENTS       = 50.0f;
//
//// Mute DSP après vibration
//static volatile uint32_t g_hapticMuteUntilMs = 0;
//static const uint32_t HAPTIC_DSP_MUTE_MS = 120;
//
//// Anti-burst soft (éviter spam I2C/standby)
//static uint32_t g_lastHapticIoMs = 0;
//static bool g_drvStandbyState = true;
//
//// ===============================
////   Anti-secteur 50 Hz
//// ===============================
//static const float MAINS_REJECT_FMIN = 47.0f;
//static const float MAINS_REJECT_FMAX = 53.0f;
//static const float MAINS_REJECT_MAX_EXCESS = 0.0030f;
//
//// ===============================
////   Cordes guitare (correction octave)
//// ===============================
//static const float guitarStringsHz[] = {
//  82.4069f, 110.000f, 146.832f, 195.998f, 246.942f, 329.628f
//};
//
//// ===============================
////   NOTE DOMINANTE + MEDIANE
//// ===============================
//static const int NOTE_WINDOW = 5;
//static float g_freqWin[NOTE_WINDOW];
//static int   g_midiWin[NOTE_WINDOW];
//static int   g_winIdx = 0;
//static int   g_winCount = 0;
//static int   g_lockedMidi = -1;
//
//// ===============================
////   STATS (debug léger)
//// ===============================
//static uint32_t g_statRejectNoPitch = 0;
//static uint32_t g_statRejectYin     = 0;
//static uint32_t g_statRejectMains   = 0;
//static uint32_t g_statRejectImpl    = 0;
//static uint32_t g_statFramesOK      = 0;
//static uint32_t g_lastStatsMs       = 0;
//
//// ===============================
////   Helpers généraux
//// ===============================
//static void initHannWindow() {
//  for (int i = 0; i < YIN_BUFFER_SIZE; i++) {
//    g_hann[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float)i / (float)(YIN_BUFFER_SIZE - 1));
//  }
//  g_hannInit = true;
//}
//
//static inline float median3(float a, float b, float c) {
//  if (a > b) { float t = a; a = b; b = t; }
//  if (b > c) { float t = b; b = c; c = t; }
//  if (a > b) { float t = a; a = b; b = t; }
//  return b;
//}
//
//static void resetPitchTrackingQuick() {
//  g_candidateFreq = 0.0f;
//  g_candidateCount = 0;
//  g_lastValidDetectMs = 0;
//}
//
//static void resetFreqSmoother() {
//  g_havePrev1 = g_havePrev2 = false;
//  g_fEmaInit = false;
//}
//
//static void resetDetectionState() {
//  resetPitchTrackingQuick();
//  resetFreqSmoother();
//  g_lastAcceptedFreq = 0.0f;
//  g_centsInit = false;
//  g_hasPitch = false;
//  g_winCount = 0;
//  g_lockedMidi = -1;
//}
//
//static bool isCloseFreq(float a, float b, float relTol = 0.04f) {
//  if (a <= 0.0f || b <= 0.0f) return false;
//  float r = a / b;
//  if (r < 1.0f) r = 1.0f / r;
//  return r <= (1.0f + relTol);
//}
//
//static bool isFrequencyPlausible(float f, Instrument p) {
//  if (g_lastAcceptedFreq <= 0.0f) return true;
//  float ratio = f / g_lastAcceptedFreq;
//  if (ratio < 1.0f) ratio = 1.0f / ratio;
//  if (p == GUITARE) return ratio <= 2.2f;
//  return ratio <= 1.6f;
//}
//
//static float smoothFrequencyRobust(float fRaw) {
//  float fMed = fRaw;
//  if (!g_havePrev1) {
//    g_fPrev1 = fRaw; g_havePrev1 = true;
//  } else if (!g_havePrev2) {
//    g_fPrev2 = g_fPrev1; g_fPrev1 = fRaw; g_havePrev2 = true;
//  } else {
//    fMed = median3(fRaw, g_fPrev1, g_fPrev2);
//    g_fPrev2 = g_fPrev1;
//    g_fPrev1 = fRaw;
//  }
//
//  if (!g_fEmaInit) {
//    g_fEma = fMed;
//    g_fEmaInit = true;
//  } else {
//    g_fEma = (1.0f - FREQ_EMA_ALPHA) * g_fEma + FREQ_EMA_ALPHA * fMed;
//  }
//  return g_fEma;
//}
//
//static float centsToRef(float f, float ref) {
//  return 1200.0f * log2f(f / ref);
//}
//
//static float bestAbsCentsToGuitarStrings(float f) {
//  float best = 1e9f;
//  for (float ref : guitarStringsHz) {
//    float c = fabsf(centsToRef(f, ref));
//    if (c < best) best = c;
//  }
//  return best;
//}
//
//static float correctOctaveForGuitar(float f) {
//  if (profilActuel != GUITARE) return f;
//
//  float f1 = f;
//  float f2 = f * 2.0f;
//
//  bool f1ok = (f1 >= 80.0f && f1 <= 700.0f);
//  bool f2ok = (f2 >= 80.0f && f2 <= 700.0f);
//
//  float c1 = f1ok ? bestAbsCentsToGuitarStrings(f1) : 1e9f;
//  float c2 = f2ok ? bestAbsCentsToGuitarStrings(f2) : 1e9f;
//
//  if (c2 + 8.0f < c1) return f2;
//  return f1;
//}
//
//// ===============================
////   Haptics helpers
//// ===============================
//static void hapticsHardOff() {
//  ledcWrite(HAP_PWM_PIN, 0);
//
//  uint32_t now = millis();
//  if (g_drvOk && !g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
//    i2cLock();
//    bool ok = drvStandby(true);
//    i2cUnlock();
//    if (ok) {
//      g_drvStandbyState = true;
//      g_lastHapticIoMs = now;
//    }
//  }
//}
//
//static void hapticsOn(uint8_t duty) {
//  if (!g_drvOk) {
//    ledcWrite(HAP_PWM_PIN, 0);
//    return;
//  }
//
//  uint32_t now = millis();
//
//  if (g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
//    i2cLock();
//    bool ok = drvStandby(false);
//    i2cUnlock();
//    if (ok) {
//      g_drvStandbyState = false;
//      g_lastHapticIoMs = now;
//    }
//  }
//
//  ledcWrite(HAP_PWM_PIN, duty);
//
//  // mute DSP court après excitation haptique
//  g_hapticMuteUntilMs = now + HAPTIC_DSP_MUTE_MS;
//}
//
//static void updateHapticsFromCents(float cents) {
//  if (!g_hasPitch || (millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) {
//    hapticsHardOff();
//    return;
//  }
//
//  float a = fabsf(cents);
//  if (a > HAP_MAXCENTS) a = HAP_MAXCENTS;
//
//  static bool vibOn = false;
//  const float ON_TH  = 6.0f;
//  const float OFF_TH = 4.5f;
//
//  if (!vibOn) vibOn = (a >= ON_TH);
//  else        vibOn = (a >= OFF_TH);
//
//  if (!vibOn) { hapticsHardOff(); return; }
//
//  float x = 0.0f;
//  if (a <= HAP_DEADBAND_CENTS) x = 0.0f;
//  else x = (a - HAP_DEADBAND_CENTS) / (HAP_MAXCENTS - HAP_DEADBAND_CENTS);
//  if (x < 0) x = 0;
//  if (x > 1) x = 1;
//
//  float y = 3.0f * x * x - 2.0f * x * x * x; // smoothstep
//
//  const uint8_t DUTY_MIN = 35;
//  const uint8_t DUTY_MAX = 210;
//  uint8_t duty = (uint8_t)(DUTY_MIN + y * (DUTY_MAX - DUTY_MIN));
//
//  const uint32_t periodMs = 100;
//  uint32_t t = millis() % periodMs;
//
//  uint32_t onBase  = (cents > 0) ? 22 : 50; // + court / - long
//  uint32_t onBonus = (uint32_t)(y * 20.0f);
//  uint32_t onMs    = onBase + onBonus;
//  if (onMs > 80) onMs = 80;
//
//  if (t < onMs) hapticsOn(duty);
//  else          hapticsHardOff();
//}
//
//// ===============================
////   AUDIO utils
//// ===============================
//static float computeRMS(const float *buffer, size_t length) {
//  double sumSq = 0;
//  for (size_t i = 0; i < length; i++) sumSq += (double)buffer[i] * buffer[i];
//  return (float)sqrt(sumSq / length);
//}
//
//// Gate adaptatif dynamique
//static bool computeAdaptiveGate(float rms, float &gateUsed) {
//  if (!g_noiseEmaInit) {
//    g_noiseEma = g_gateRms;
//    g_noiseEmaInit = true;
//  } else {
//    float silenceLimit = g_gateRms * 0.85f;
//    if (rms < silenceLimit) {
//      g_noiseEma = (1.0f - NOISE_EMA_ALPHA) * g_noiseEma + NOISE_EMA_ALPHA * rms;
//    }
//  }
//
//  float gate = g_noiseEma * 2.5f;
//  if (gate < SILENCE_RMS_THRES) gate = SILENCE_RMS_THRES;
//  if (gate > 0.02f) gate = 0.02f;
//
//  gateUsed = gate;
//  return (rms >= gate);
//}
//
//static void changerProfil(Instrument p) {
//  profilActuel = p;
//  Config c = reglages[p];
//
//  float fHP = c.fMin * 0.8f;
//  float fLP = c.fMax * 1.2f;
//
//  g_hpAlpha = expf(-2.0f * 3.1415926f * fHP / SAMPLE_RATE);
//  g_lpAlpha = expf(-2.0f * 3.1415926f * fLP / SAMPLE_RATE);
//
//  g_hpY = 0; g_hpXprev = 0; g_lpY = 0;
//
//  for (int i = 0; i < NOTE_WINDOW; i++) { g_freqWin[i] = 0; g_midiWin[i] = -9999; }
//  g_winIdx = 0; g_winCount = 0; g_lockedMidi = -1;
//
//  resetDetectionState();
//  g_noiseEmaInit = false;
//  g_hapticMuteUntilMs = 0;
//
//  setInstrumentColor(p);
//  DBG_PRINTF("\n>>> Profil: %s (%.0fHz - %.0fHz)\n", c.nom, c.fMin, c.fMax);
//}
//
//static void appliquerFiltre(float *buffer, int taille) {
//  if (!g_hannInit) initHannWindow();
//
//  for (int i = 0; i < taille; i++) {
//    float x = buffer[i];
//    float yhp = g_hpAlpha * (g_hpY + x - g_hpXprev);
//    g_hpY = yhp; g_hpXprev = x;
//
//    float ylp = g_lpAlpha * g_lpY + (1.0f - g_lpAlpha) * yhp;
//    g_lpY = ylp;
//
//    buffer[i] = ylp * g_hann[i];
//  }
//}
//
//static float detectPitchYIN(const float *input, float *outYinMin = nullptr) {
//  int tauMin = (int)(SAMPLE_RATE / reglages[profilActuel].fMax);
//  int tauMax = (int)(SAMPLE_RATE / reglages[profilActuel].fMin);
//  if (tauMax >= YIN_BUFFER_SIZE - 2) tauMax = YIN_BUFFER_SIZE - 2;
//  if (tauMin < 2) tauMin = 2;
//
//  for (int tau = 0; tau <= tauMax; tau++) {
//    double sum = 0;
//    for (int i = 0; i < YIN_BUFFER_SIZE - tau; i++) {
//      float d = input[i] - input[i + tau];
//      sum += (double)d * (double)d;
//    }
//    g_yinBuffer[tau] = (float)sum;
//  }
//
//  g_yinBuffer[0] = 1.0f;
//  double runningSum = 0;
//  float yinMin = 1.0f;
//
//  for (int tau = 1; tau <= tauMax; tau++) {
//    runningSum += g_yinBuffer[tau];
//    g_yinBuffer[tau] = (float)(g_yinBuffer[tau] * tau / (float)runningSum);
//    if (tau >= tauMin && g_yinBuffer[tau] < yinMin) yinMin = g_yinBuffer[tau];
//  }
//
//  const float threshold = 0.15f;
//  int tauFound = -1;
//  for (int tau = tauMin; tau <= tauMax; tau++) {
//    if (g_yinBuffer[tau] < threshold) {
//      while (tau + 1 <= tauMax && g_yinBuffer[tau + 1] < g_yinBuffer[tau]) tau++;
//      tauFound = tau;
//      break;
//    }
//  }
//
//  if (outYinMin) *outYinMin = yinMin;
//  if (tauFound < 0) return -1.0f;
//
//  float s0 = g_yinBuffer[tauFound - 1];
//  float s1 = g_yinBuffer[tauFound];
//  float s2 = g_yinBuffer[tauFound + 1];
//
//  float denom = (2.0f * s1 - s2 - s0);
//  float delta = 0.0f;
//  if (fabsf(denom) > 1e-9f) delta = 0.5f * (s2 - s0) / denom;
//
//  float tauInterp = tauFound + delta;
//
//  for (int k = 2; k <= 4; k++) {
//    int t = (int)(tauInterp / k);
//    if (t >= tauMin && g_yinBuffer[t] < 0.20f) tauInterp /= k;
//  }
//
//  if (tauInterp <= 0.0f) return -1.0f;
//  return SAMPLE_RATE / tauInterp;
//}
//
//static int freqToMidi(float freq) {
//  float n = 12.0f * log2f(freq / 440.0f);
//  return (int)roundf(n) + 69;
//}
//
//static float midiToFreq(int midi) {
//  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
//}
//
//static void midiToNoteName(int midi, const char **note, int *oct) {
//  static const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
//  int idx = ((midi % 12) + 12) % 12;
//  *note = names[idx];
//  *oct = midi / 12 - 1;
//}
//
//static float centsAgainstMidi(float freq, int midiRef) {
//  float ref = midiToFreq(midiRef);
//  return 1200.0f * log2f(freq / ref);
//}
//
//static int dominantMidi(const int *midiArr, int n, int *outCount) {
//  int bestMidi = midiArr[0];
//  int bestCount = 1;
//  for (int i = 0; i < n; i++) {
//    int m = midiArr[i];
//    int count = 0;
//    for (int j = 0; j < n; j++) if (midiArr[j] == m) count++;
//    if (count > bestCount) { bestCount = count; bestMidi = m; }
//  }
//  if (outCount) *outCount = bestCount;
//  return bestMidi;
//}
//
//static float medianFloat(float *arr, int n) {
//  for (int i = 0; i < n - 1; i++) {
//    for (int j = i + 1; j < n; j++) {
//      if (arr[j] < arr[i]) { float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp; }
//    }
//  }
//  if (n <= 0) return 0.0f;
//  if (n % 2 == 1) return arr[n / 2];
//  return 0.5f * (arr[n/2 - 1] + arr[n/2]);
//}
//
//// ===============================
////   I2S init/read
//// ===============================
//static bool initI2S() {
//  i2s_config_t cfg = {};
//  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
//  cfg.sample_rate = (int)SAMPLE_RATE;
//  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
//  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
//  cfg.communication_format = I2S_COMM_FORMAT_I2S;
//  cfg.dma_buf_count = 8;
//  cfg.dma_buf_len = 256;
//  cfg.use_apll = false;
//  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
//
//  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
//
//  i2s_pin_config_t pins = {
//    .bck_io_num = I2S_BCK_PIN,
//    .ws_io_num  = I2S_WS_PIN,
//    .data_out_num = I2S_PIN_NO_CHANGE,
//    .data_in_num  = I2S_DATA_PIN
//  };
//  return i2s_set_pin(I2S_PORT, &pins) == ESP_OK;
//}
//
//static size_t readI2SBlock(float *buffer, size_t n) {
//  static int32_t raw[256];
//  size_t read = 0;
//  while (read < n) {
//    size_t bytes = 0;
//    i2s_read(I2S_PORT, raw, sizeof(raw), &bytes, portMAX_DELAY);
//    for (size_t i = 0; i < bytes / 4 && read < n; i++) {
//      buffer[read++] = (raw[i] >> 8) / 8388608.0f;
//    }
//  }
//  return read;
//}
//
//// ===============================
////   Battery
//// ===============================
//static bool readBatterySafe(float &vbat, float &soc) {
//  for (int attempt = 0; attempt < 5; attempt++) {
//    vbat = maxlipo.cellVoltage();
//    soc  = maxlipo.cellPercent();
//    bool okV = !isnan(vbat) && (vbat > 2.5f) && (vbat < 4.5f);
//    bool okS = !isnan(soc)  && (soc >= 0.0f) && (soc <= 100.0f);
//    if (okV && okS) return true;
//    delay(60);
//  }
//  return false;
//}
//
//#if ENABLE_BATTERY_SIM_SERIAL
//static void requestBatteryRefresh() {
//  g_battForceRefresh = true;
//}
//
//static void printBatteryHelp() {
//  DBG_PRINTLN("\n[BAT TEST] Commandes:");
//  DBG_PRINTLN("  bat help        -> aide");
//  DBG_PRINTLN("  bat sim         -> mode simulation");
//  DBG_PRINTLN("  bat real        -> mode batterie reelle");
//  DBG_PRINTLN("  bat soc <0-100> -> force SOC simule");
//  DBG_PRINTLN("  bat vbat <V>    -> force tension simulee");
//  DBG_PRINTLN("  bat show        -> affiche l'etat courant");
//}
//
//static void handleBatterySerialCommands() {
//  if (!Serial.available()) return;
//
//  String line = Serial.readStringUntil('\n');
//  line.trim();
//  if (line.length() == 0) return;
//
//  String lower = line;
//  lower.toLowerCase();
//
//  if (!lower.startsWith("bat")) return;
//
//  if (lower == "bat" || lower == "bat help") {
//    printBatteryHelp();
//    return;
//  }
//
//  if (lower == "bat sim") {
//    g_battSimMode = true;
//    requestBatteryRefresh();
//    DBG_PRINTLN("[BAT] Mode simulation active");
//    return;
//  }
//
//  if (lower == "bat real") {
//    g_battSimMode = false;
//    requestBatteryRefresh();
//    DBG_PRINTLN("[BAT] Retour mode batterie reelle");
//    return;
//  }
//
//  if (lower.startsWith("bat soc ")) {
//    float val = lower.substring(8).toFloat();
//    if (val < 0.0f) val = 0.0f;
//    if (val > 100.0f) val = 100.0f;
//
//    g_battSimMode = true;
//    g_battSimSoc = val;
//    requestBatteryRefresh();
//
//    DBG_PRINTF("[BAT] SOC simule = %.1f %%\n", val);
//    return;
//  }
//
//  if (lower.startsWith("bat vbat ")) {
//    float val = lower.substring(9).toFloat();
//    if (val < 2.5f) val = 2.5f;
//    if (val > 4.5f) val = 4.5f;
//
//    g_battSimMode = true;
//    g_battSimVbat = val;
//    requestBatteryRefresh();
//
//    DBG_PRINTF("[BAT] VBAT simulee = %.3f V\n", val);
//    return;
//  }
//
//  if (lower == "bat show") {
//    DBG_PRINTF("[BAT] Mode=%s | g_battOk=%d | SOCsim=%.1f%% | VBATsim=%.3fV\n",
//               g_battSimMode ? "SIM" : "REAL",
//               (int)g_battOk,
//               g_battSimSoc,
//               g_battSimVbat);
//    return;
//  }
//
//  DBG_PRINTLN("[BAT] Commande inconnue. Tape: bat help");
//}
//#endif
//
//// ===============================
////   Gate calibrage (boot)
//// ===============================
//static void calibrateNoiseFloor() {
//  const int frames = 20;
//  double acc = 0;
//
//  hapticsHardOff();
//
//  for (int i = 0; i < frames; i++) {
//    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
//    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
//    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
//    acc += rms;
//    vTaskDelay(pdMS_TO_TICKS(10));
//  }
//
//  g_noiseFloor = (float)(acc / frames);
//  g_gateRms = fmaxf(SILENCE_RMS_THRES, g_noiseFloor * 2.5f);
//
//  g_noiseEma = g_gateRms;
//  g_noiseEmaInit = true;
//
//  DBG_PRINTF("NoiseFloor=%.6f -> GateBase=%.6f\n", g_noiseFloor, g_gateRms);
//}
//
//// ===============================
////   TASKS
//// ===============================
//static void TaskBattery(void *pv) {
//  (void)pv;
//
//  uint32_t nextReadMs = millis(); // lecture immédiate au démarrage
//
//  for (;;) {
//    uint32_t now = millis();
//    bool doRead = false;
//
//    if ((int32_t)(now - nextReadMs) >= 0) {
//      doRead = true;
//    }
//
//    #if ENABLE_BATTERY_SIM_SERIAL
//    if (g_battForceRefresh) {
//      g_battForceRefresh = false;
//      doRead = true;
//    }
//    #endif
//
//    if (doRead) {
//      float v = NAN, s = NAN;
//      bool ok = false;
//      bool simUsed = false;
//
//      #if ENABLE_BATTERY_SIM_SERIAL
//      if (g_battSimMode) {
//        v = g_battSimVbat;
//        s = g_battSimSoc;
//        ok = true;
//        simUsed = true;
//      } else
//      #endif
//      {
//        if (g_battOk) {
//          i2cLock();
//          ok = readBatterySafe(v, s);
//          i2cUnlock();
//        } else {
//          ok = false;
//        }
//      }
//
//      if (ok) {
//        setBatteryColor(s);
//        DBG_PRINTF("[BAT] %s VBAT=%.3fV SOC=%.1f%%\n", simUsed ? "(SIM)" : "(REAL)", v, s);
//      } else {
//        setBatteryColor(NAN);
//        DBG_PRINTLN("[BAT] lecture echec");
//      }
//
//      nextReadMs = millis() + BATT_PERIOD_MS;
//    }
//
//    vTaskDelay(pdMS_TO_TICKS(100));
//  }
//}
//
//static void TaskUI(void *pv) {
//  (void)pv;
//  int last = digitalRead(BTN_PIN);
//  uint32_t lastChange = millis();
//
//  for (;;) {
//    int now = digitalRead(BTN_PIN);
//    if (now != last) { last = now; lastChange = millis(); }
//
//    if ((millis() - lastChange) > 30) {
//      if (now == LOW) {
//        Instrument p = (Instrument)(((int)profilActuel + 1) % 5);
//        changerProfil(p);
//        while (digitalRead(BTN_PIN) == LOW) vTaskDelay(pdMS_TO_TICKS(20));
//      }
//    }
//    vTaskDelay(pdMS_TO_TICKS(10));
//  }
//}
//
//static void TaskAudio(void *pv) {
//  (void)pv;
//
//  static bool gateOpen = false;
//
//  const float ALPHA = 0.12f;
//  const float MAX_STEP_CENTS = 2.0f;
//
//  for (;;) {
//    if (!g_drvOk) {
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(20));
//      continue;
//    }
//
//    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
//    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
//
//    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
//    g_lastRms = rms;
//
//    float gateUsed = g_gateRms;
//    bool hasSignal = computeAdaptiveGate(rms, gateUsed);
//
//    const float gateOn  = gateUsed;
//    const float gateOff = gateUsed * 0.70f;
//
//    if (!gateOpen) gateOpen = hasSignal;
//    else           gateOpen = (rms >= gateOff);
//
//    (void)gateOn;
//
//    if (!gateOpen) {
//      resetDetectionState();
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (millis() < (uint32_t)g_hapticMuteUntilMs) {
//      resetPitchTrackingQuick();
//      g_hasPitch = false;
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (g_lastValidDetectMs != 0 && (millis() - g_lastValidDetectMs) > 350) {
//      resetPitchTrackingQuick();
//      resetFreqSmoother();
//      g_lastAcceptedFreq = 0.0f;
//    }
//
//    float yinMin = 1.0f;
//    float f_micro = detectPitchYIN(g_audioBuffer, &yinMin);
//    g_lastYin = yinMin;
//
//    const float YIN_GOOD = reglages[profilActuel].yinGood;
//    bool pitchValid = (f_micro > 0.0f) && (yinMin > 0.0f) && (yinMin <= YIN_GOOD);
//
//    if (!pitchValid) {
//      g_statRejectYin++;
//      g_hasPitch = false;
//      if ((millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    {
//      float relTol = (profilActuel == CHROMATIQUE) ? 0.025f : 0.04f;
//      uint8_t confirmNeeded = (profilActuel == CHROMATIQUE) ? 4 : 3;
//
//      if (g_candidateCount == 0) {
//        g_candidateFreq = f_micro;
//        g_candidateCount = 1;
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//
//      if (!isCloseFreq(f_micro, g_candidateFreq, relTol)) {
//        g_candidateFreq = f_micro;
//        g_candidateCount = 1;
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//
//      g_candidateCount++;
//      g_candidateFreq = 0.7f * g_candidateFreq + 0.3f * f_micro;
//
//      if (g_candidateCount < confirmNeeded) {
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//    }
//
//    float f = g_candidateFreq;
//    g_candidateCount = 0;
//    g_lastValidDetectMs = millis();
//
//    if (profilActuel == GUITARE) {
//      f = correctOctaveForGuitar(f);
//    }
//
//    float rmsExcess = rms - g_noiseEma;
//    if (profilActuel == CHROMATIQUE &&
//        f >= MAINS_REJECT_FMIN && f <= MAINS_REJECT_FMAX &&
//        rmsExcess < MAINS_REJECT_MAX_EXCESS) {
//      g_statRejectMains++;
//      g_hasPitch = false;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (!isFrequencyPlausible(f, profilActuel)) {
//      g_statRejectImpl++;
//      g_hasPitch = false;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    f = smoothFrequencyRobust(f);
//
//    g_lastAcceptedFreq = f;
//    g_lastFreq = f;
//    g_hasPitch = true;
//    g_lastPitchMs = millis();
//
//    int midi = freqToMidi(f);
//    g_freqWin[g_winIdx] = f;
//    g_midiWin[g_winIdx] = midi;
//    g_winIdx = (g_winIdx + 1) % NOTE_WINDOW;
//    if (g_winCount < NOTE_WINDOW) g_winCount++;
//
//    if (g_winCount < 2) {
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    int midis[NOTE_WINDOW];
//    int n = g_winCount;
//    for (int i = 0; i < n; i++) midis[i] = g_midiWin[i];
//
//    int domCount = 0;
//    int domMidi = dominantMidi(midis, n, &domCount);
//
//    const float LOCK_RATIO = 0.6f;
//    if (g_lockedMidi < 0) g_lockedMidi = domMidi;
//    else if (domMidi != g_lockedMidi) {
//      if ((float)domCount >= LOCK_RATIO * n) g_lockedMidi = domMidi;
//    }
//
//    float centsList[NOTE_WINDOW];
//    int cN = 0;
//    for (int i = 0; i < n; i++) {
//      float ff = g_freqWin[i];
//      if (ff <= 0) continue;
//      float c = centsAgainstMidi(ff, g_lockedMidi);
//      if (fabsf(c) > 80.0f) continue;
//      centsList[cN++] = c;
//    }
//
//    if (cN < 1) {
//      g_statRejectNoPitch++;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    float centsMed = medianFloat(centsList, cN);
//
//    if (!g_centsInit) {
//      g_centsSmooth = centsMed;
//      g_centsInit = true;
//    } else {
//      float target = (1.0f - ALPHA) * g_centsSmooth + ALPHA * centsMed;
//      float delta = target - g_centsSmooth;
//      if (delta >  MAX_STEP_CENTS) delta =  MAX_STEP_CENTS;
//      if (delta < -MAX_STEP_CENTS) delta = -MAX_STEP_CENTS;
//      g_centsSmooth += delta;
//    }
//
//    g_lastCents = g_centsSmooth;
//    updateHapticsFromCents(g_centsSmooth);
//
//    const char *noteName;
//    int oct;
//    midiToNoteName(g_lockedMidi, &noteName, &oct);
//    float ref = midiToFreq(g_lockedMidi);
//
//    g_statFramesOK++;
//
//    DBG_PRINTF("[%s] f=%.2fHz note=%s%d ref=%.2fHz centsMed=%+.2f centsSm=%+.2f "
//               "N=%d dom=%d/%d rms=%.5f gate=%.5f nEma=%.5f yin=%.3f\n",
//               reglages[profilActuel].nom, f, noteName, oct, ref, centsMed, g_centsSmooth,
//               cN, domCount, n, rms, gateUsed, g_noiseEma, yinMin);
//
//    uint32_t now = millis();
//    if (now - g_lastStatsMs > 5000) {
//      DBG_PRINTF("STATS ok=%lu rejYIN=%lu rejMains=%lu rejImpl=%lu rejNoPitch=%lu\n",
//                 (unsigned long)g_statFramesOK,
//                 (unsigned long)g_statRejectYin,
//                 (unsigned long)g_statRejectMains,
//                 (unsigned long)g_statRejectImpl,
//                 (unsigned long)g_statRejectNoPitch);
//      g_lastStatsMs = now;
//    }
//
//    vTaskDelay(pdMS_TO_TICKS(5));
//  }
//}
//
//// ===============================
////   SETUP
//// ===============================
//void setup() {
//  DBG_BEGIN(115200);
//  delay(150);
//
//  #if ENABLE_BATTERY_SIM_SERIAL
//  printBatteryHelp();
//  #endif
//
//  if (!g_hannInit) initHannWindow();
//
//  pinMode(HAP_PWM_PIN, OUTPUT);
//  digitalWrite(HAP_PWM_PIN, LOW);
//  delay(50);
//
//  pinMode(BTN_PIN, INPUT);
//
//  pwmAttach(LED_BATT_R, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_BATT_G, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_BATT_B, LED_PWM_FREQ, LED_PWM_RES);
//
//  pwmAttach(LED_INST_R, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_INST_G, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_INST_B, LED_PWM_FREQ, LED_PWM_RES);
//
//  setBattRGB(255, 0, 0);
//
//  pwmAttach(HAP_PWM_PIN, HAP_PWM_FREQ, HAP_PWM_RES);
//  ledcWrite(HAP_PWM_PIN, 0);
//
//  g_i2cMutex = xSemaphoreCreateMutex();
//
//  Wire.begin(I2C_SDA, I2C_SCL);
//  Wire.setClock(I2C_FREQ);
//  delay(200);
//
//  changerProfil(GUITARE);
//
//  bool i2sOk = initI2S();
//  if (!i2sOk) DBG_PRINTLN("ERREUR: initI2S() a echoue");
//
//  float vbat = NAN, soc = NAN;
//  bool ok = false;
//
//  i2cLock();
//  g_battOk = maxlipo.begin();
//  if (g_battOk) {
//    maxlipo.wake();
//    maxlipo.quickStart();
//    delay(400);
//    ok = readBatterySafe(vbat, soc);
//  }
//  i2cUnlock();
//
//  if (!g_battOk || !ok) {
//    DBG_PRINTLN("BATTERIE: lecture echec.");
//    setBatteryColor(NAN);
//  } else {
//    DBG_PRINTF("BATTERIE: VBAT=%.3fV SOC=%.1f%%\n", vbat, soc);
//    setBatteryColor(soc);
//  }
//
//  bool drvOk = false;
//  i2cLock();
//  drvOk = drvInitPwmInput();
//  if (drvOk) {
//    drvDump();
//    drvOk = drvStandby(true); // hard-off par défaut
//  }
//  i2cUnlock();
//
//  g_drvOk = drvOk;
//  g_drvStandbyState = true;
//  DBG_PRINTLN(drvOk ? "DRV2605L: OK + STANDBY (hard-off)" : "DRV2605L: INIT ECHEC (I2C?)");
//
//  hapticsHardOff();
//
//  if (drvOk && i2sOk) calibrateNoiseFloor();
//
//  xTaskCreatePinnedToCore(TaskAudio,   "Audio",   8192, nullptr, 3, nullptr, 1);
//  xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 2, nullptr, 1);
//  xTaskCreatePinnedToCore(TaskBattery, "Battery", 4096, nullptr, 1, nullptr, 1);
//
//  DBG_PRINTLN("OK: tasks started.");
//}
//
//void loop() {
//  #if ENABLE_BATTERY_SIM_SERIAL
//  handleBatterySerialCommands();
//  #endif
//
//  vTaskDelay(pdMS_TO_TICKS(20));
//}




//#include <Arduino.h>
//#include "driver/i2s.h"
//#include <Wire.h>
//#include "Adafruit_MAX1704X.h"
//#include <math.h>
//
//// ===============================
////   SERIAL DEBUG
//// ===============================
//#define DEBUG_SERIAL 1
//#if DEBUG_SERIAL
//  #define DBG_BEGIN(x)    Serial.begin(x)
//  #define DBG_PRINTLN(x)  Serial.println(x)
//  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
//#else
//  #define DBG_BEGIN(x)
//  #define DBG_PRINTLN(x)
//  #define DBG_PRINTF(...)
//#endif
//
//// ===============================
////   PROFILS INSTRUMENTS
//// ===============================
//enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
//volatile Instrument profilActuel = GUITARE;
//
//struct Config {
//  float fMin;
//  float fMax;
//  const char* nom;
//  float yinGood;     // seuil YIN par profil (plus bas = plus strict)
//};
//
//static Config reglages[] = {
//  {190.0f, 700.0f,  "Violon",     0.20f},
//  {80.0f,  700.0f,  "Guitare",    0.20f},
//  {260.0f, 700.0f,  "Ukulele",    0.20f},
//  {30.0f,  200.0f,  "Basse",      0.26f},
//  {30.0f,  2000.0f, "Chromatique",0.20f}
//};
//
//// ===============================
////   I2C
//// ===============================
//static const int I2C_SDA = 21;
//static const int I2C_SCL = 22;
//static const uint32_t I2C_FREQ = 50000;
//
//static SemaphoreHandle_t g_i2cMutex = nullptr;
//static inline void i2cLock()   { if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
//static inline void i2cUnlock() { if (g_i2cMutex) xSemaphoreGive(g_i2cMutex); }
//
//// ===============================
////   DRV2605L
//// ===============================
//static const uint8_t DRV_ADDR = 0x5A;
//
//static const int HAP_PWM_PIN  = 26;
//static const int HAP_PWM_FREQ = 1000;
//static const int HAP_PWM_RES  = 8;
//
//static const uint8_t REG_MODE = 0x01;   // bit6 = STANDBY
//
//static bool drvWriteReg(uint8_t reg, uint8_t val) {
//  for (int attempt = 0; attempt < 3; attempt++) {
//    Wire.beginTransmission(DRV_ADDR);
//    Wire.write(reg);
//    Wire.write(val);
//    if (Wire.endTransmission(true) == 0) return true;
//    delay(5);
//  }
//  return false;
//}
//
//static uint8_t drvReadReg(uint8_t reg) {
//  Wire.beginTransmission(DRV_ADDR);
//  Wire.write(reg);
//  if (Wire.endTransmission(false) != 0) return 0xFF;
//  Wire.requestFrom(DRV_ADDR, (uint8_t)1);
//  return Wire.available() ? Wire.read() : 0xFF;
//}
//
//static bool drvStandby(bool en) {
//  uint8_t m = drvReadReg(REG_MODE);
//  if (m == 0xFF) return false;
//  if (en) m |=  (1 << 6);
//  else    m &= ~(1 << 6);
//  return drvWriteReg(REG_MODE, m);
//}
//
//static bool drvInitPwmInput() {
//  if (!drvWriteReg(0x01, 0x80)) return false; // reset
//  delay(10);
//
//  if (!drvWriteReg(0x01, 0x03)) return false; // MODE=3 PWM/Analog
//  delay(5);
//
//  uint8_t c1 = drvReadReg(0x1A);
//  if (c1 == 0xFF) return false;
//  c1 &= (uint8_t)~(1 << 7); // ERM
//  if (!drvWriteReg(0x1A, c1)) return false;
//
//  uint8_t c3 = drvReadReg(0x1D);
//  if (c3 == 0xFF) return false;
//  c3 |=  (uint8_t)(1 << 5); // ERM_OPEN_LOOP
//  c3 &= (uint8_t)~(1 << 1); // PWM input
//  if (!drvWriteReg(0x1D, c3)) return false;
//
//  if (!drvWriteReg(0x17, 0x8C)) return false; // OD_CLAMP
//  return true;
//}
//
//static void drvDump() {
//  uint8_t r01 = drvReadReg(0x01);
//  uint8_t r1a = drvReadReg(0x1A);
//  uint8_t r1d = drvReadReg(0x1D);
//  DBG_PRINTF("DRV regs: MODE=0x%02X FB=0x%02X CTL3=0x%02X\n", r01, r1a, r1d);
//}
//
//// ===============================
////   BATTERY (MAX17048)
//// ===============================
//Adafruit_MAX17048 maxlipo;
//static bool g_battOk = false;
//
//static const float ORANGE_PCT = 30.0f;
//static const float RED_PCT    = 10.0f;
//static const uint32_t BATT_PERIOD_MS = 60000;
//
//static const int LED_BATT_R = 27;
//static const int LED_BATT_G = 12;
//static const int LED_BATT_B = 14;
//
//// ===============================
////   LED INSTRUMENT
//// ===============================
//static const int LED_INST_R = 32;
//static const int LED_INST_G = 13;
//static const int LED_INST_B = 33;
//
//static const int BTN_PIN = 34;
//
//// ===============================
////   LEDC
//// ===============================
//static const int LED_PWM_FREQ = 5000;
//static const int LED_PWM_RES  = 8;
//
//static inline void pwmAttach(int pin, int freq, int res) {
//  ledcAttach(pin, freq, res);
//  ledcWrite(pin, 0);
//}
//
//// ===============================
////   LED helpers
//// ===============================
//static inline void setBattRGB(uint8_t r, uint8_t g, uint8_t b) {
//  ledcWrite(LED_BATT_R, r);
//  ledcWrite(LED_BATT_G, g);
//  ledcWrite(LED_BATT_B, b);
//}
//static inline void setInstRGB(uint8_t r, uint8_t g, uint8_t b) {
//  ledcWrite(LED_INST_R, r);
//  ledcWrite(LED_INST_G, g);
//  ledcWrite(LED_INST_B, b);
//}
//static inline void setBatteryColor(float soc) {
//  if (!g_battOk || isnan(soc)) { setBattRGB(255, 0, 255); return; }
//  if (soc <= RED_PCT)          { setBattRGB(255, 0, 0);   return; }
//  if (soc <= ORANGE_PCT)       { setBattRGB(255, 80, 0);  return; }
//  setBattRGB(0, 255, 0);
//}
//static inline void setInstrumentColor(Instrument p) {
//  switch (p) {
//    case GUITARE:     setInstRGB(0, 0, 255);   break;
//    case UKULELE:     setInstRGB(0, 255, 255); break;
//    case BASSE:       setInstRGB(255, 0, 0);   break;
//    case VIOLON:      setInstRGB(255, 0, 255); break;
//    case CHROMATIQUE: setInstRGB(0, 255, 0);   break;
//  }
//}
//
//// ===============================
////   AUDIO I2S
//// ===============================
//static const i2s_port_t I2S_PORT   = I2S_NUM_0;
//static const int I2S_BCK_PIN       = 5;
//static const int I2S_WS_PIN        = 25;
//static const int I2S_DATA_PIN      = 35;
//
//static const float SAMPLE_RATE     = 20000.0f;
//static const int   YIN_BUFFER_SIZE = 1024;
//
//static float g_audioBuffer[YIN_BUFFER_SIZE];
//static float g_yinBuffer[YIN_BUFFER_SIZE];
//
//// Fenêtre de Hann
//static float g_hann[YIN_BUFFER_SIZE];
//static bool  g_hannInit = false;
//
//// filtres HP/LP
//static float g_hpAlpha, g_lpAlpha;
//static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;
//
//// Gate adaptatif dynamique
//static const float SILENCE_RMS_THRES = 0.0012f;
//static float g_noiseFloor = 0.0f;
//static float g_gateRms    = SILENCE_RMS_THRES;
//static bool  g_noiseEmaInit = false;
//static float g_noiseEma     = 0.0f;
//static const float NOISE_EMA_ALPHA = 0.02f;
//
//// ===============================
////   PARTAGE ENTRE TASKS
//// ===============================
//static volatile float g_lastFreq  = -1.0f;
//static volatile float g_lastCents = 0.0f;
//static volatile float g_lastRms   = 0.0f;
//static volatile float g_lastYin   = 1.0f;
//static volatile bool  g_hasPitch  = false;
//
//// ===============================
////   Cents smoothing + slew limiter
//// ===============================
//static float g_centsSmooth = 0.0f;
//static bool  g_centsInit   = false;
//
//// ===============================
////   Pitch tracking robustesse
//// ===============================
//static float   g_candidateFreq = 0.0f;
//static uint8_t g_candidateCount = 0;
//static uint32_t g_lastValidDetectMs = 0;
//static float   g_lastAcceptedFreq = 0.0f;
//
//// Lissage fréquence robuste (médiane3 + EMA)
//static float g_fPrev1 = 0.0f, g_fPrev2 = 0.0f;
//static bool  g_havePrev1 = false, g_havePrev2 = false;
//static float g_fEma = 0.0f;
//static bool  g_fEmaInit = false;
//static const float FREQ_EMA_ALPHA = 0.35f;
//
//// ===============================
////   HAPTICS (produit + hard-off)
//// ===============================
//static volatile bool     g_drvOk = false;
//static volatile uint32_t g_lastPitchMs = 0;
//static const uint32_t    PITCH_TIMEOUT_MS = 250;
//
//static const float HAP_DEADBAND_CENTS = 5.0f;
//static const float HAP_MAXCENTS       = 50.0f;
//
//// Mute DSP après vibration (réduit pour latence)
//static volatile uint32_t g_hapticMuteUntilMs = 0;
//static const uint32_t HAPTIC_DSP_MUTE_MS = 20;
//
//// Anti-burst soft (pour éviter spam I2C/standby)
//static uint32_t g_lastHapticIoMs = 0;
//static bool g_drvStandbyState = true;
//
//// NOUVEAU : état PWM haptique actif (pour mute uniquement au front montant)
//static bool g_hapticPwmActive = false;
//
//// ===============================
////   Anti-secteur 50 Hz
//// ===============================
//static const float MAINS_REJECT_FMIN = 47.0f;
//static const float MAINS_REJECT_FMAX = 53.0f;
//static const float MAINS_REJECT_MAX_EXCESS = 0.0030f;
//
//// ===============================
////   Corde guitare (correction octave)
//// ===============================
//static const float guitarStringsHz[] = {
//  82.4069f, 110.000f, 146.832f, 195.998f, 246.942f, 329.628f
//};
//
//// ===============================
////   NOTE DOMINANTE + MEDIANE
//// ===============================
//// Réduit de 5 -> 3 pour plus de réactivité
//static const int NOTE_WINDOW = 3;
//static float g_freqWin[NOTE_WINDOW];
//static int   g_midiWin[NOTE_WINDOW];
//static int   g_winIdx = 0;
//static int   g_winCount = 0;
//static int   g_lockedMidi = -1;
//
//// ===============================
////   STATS (debug léger)
//// ===============================
//static uint32_t g_statRejectNoPitch = 0;
//static uint32_t g_statRejectYin     = 0;
//static uint32_t g_statRejectMains   = 0;
//static uint32_t g_statRejectImpl    = 0;
//static uint32_t g_statFramesOK      = 0;
//static uint32_t g_lastStatsMs       = 0;
//
//// ===============================
////   Helpers généraux
//// ===============================
//static void initHannWindow() {
//  for (int i = 0; i < YIN_BUFFER_SIZE; i++) {
//    g_hann[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float)i / (float)(YIN_BUFFER_SIZE - 1));
//  }
//  g_hannInit = true;
//}
//
//static inline float median3(float a, float b, float c) {
//  if (a > b) { float t = a; a = b; b = t; }
//  if (b > c) { float t = b; b = c; c = t; }
//  if (a > b) { float t = a; a = b; b = t; }
//  return b;
//}
//
//static void resetPitchTrackingQuick() {
//  g_candidateFreq = 0.0f;
//  g_candidateCount = 0;
//  g_lastValidDetectMs = 0;
//}
//
//static void resetFreqSmoother() {
//  g_havePrev1 = g_havePrev2 = false;
//  g_fEmaInit = false;
//}
//
//static void resetDetectionState() {
//  resetPitchTrackingQuick();
//  resetFreqSmoother();
//  g_lastAcceptedFreq = 0.0f;
//  g_centsInit = false;
//  g_hasPitch = false;
//  g_winCount = 0;
//  g_lockedMidi = -1;
//}
//
//static bool isCloseFreq(float a, float b, float relTol = 0.04f) {
//  if (a <= 0.0f || b <= 0.0f) return false;
//  float r = a / b;
//  if (r < 1.0f) r = 1.0f / r;
//  return r <= (1.0f + relTol);
//}
//
//static bool isFrequencyPlausible(float f, Instrument p) {
//  if (g_lastAcceptedFreq <= 0.0f) return true;
//  float ratio = f / g_lastAcceptedFreq;
//  if (ratio < 1.0f) ratio = 1.0f / ratio;
//  if (p == GUITARE) return ratio <= 2.2f;
//  return ratio <= 1.6f;
//}
//
//static float smoothFrequencyRobust(float fRaw) {
//  float fMed = fRaw;
//  if (!g_havePrev1) {
//    g_fPrev1 = fRaw; g_havePrev1 = true;
//  } else if (!g_havePrev2) {
//    g_fPrev2 = g_fPrev1; g_fPrev1 = fRaw; g_havePrev2 = true;
//  } else {
//    fMed = median3(fRaw, g_fPrev1, g_fPrev2);
//    g_fPrev2 = g_fPrev1;
//    g_fPrev1 = fRaw;
//  }
//
//  if (!g_fEmaInit) {
//    g_fEma = fMed;
//    g_fEmaInit = true;
//  } else {
//    g_fEma = (1.0f - FREQ_EMA_ALPHA) * g_fEma + FREQ_EMA_ALPHA * fMed;
//  }
//  return g_fEma;
//}
//
//static float centsToRef(float f, float ref) {
//  return 1200.0f * log2f(f / ref);
//}
//
//static float bestAbsCentsToGuitarStrings(float f) {
//  float best = 1e9f;
//  for (float ref : guitarStringsHz) {
//    float c = fabsf(centsToRef(f, ref));
//    if (c < best) best = c;
//  }
//  return best;
//}
//
//static float correctOctaveForGuitar(float f) {
//  if (profilActuel != GUITARE) return f;
//
//  float f1 = f;
//  float f2 = f * 2.0f;
//
//  bool f1ok = (f1 >= 80.0f && f1 <= 700.0f);
//  bool f2ok = (f2 >= 80.0f && f2 <= 700.0f);
//
//  float c1 = f1ok ? bestAbsCentsToGuitarStrings(f1) : 1e9f;
//  float c2 = f2ok ? bestAbsCentsToGuitarStrings(f2) : 1e9f;
//
//  if (c2 + 8.0f < c1) return f2;
//  return f1;
//}
//
//// ===============================
////   Haptics helpers
//// ===============================
//static void hapticsHardOff() {
//  ledcWrite(HAP_PWM_PIN, 0);
//  g_hapticPwmActive = false;
//
//  uint32_t now = millis();
//  if (g_drvOk && !g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
//    i2cLock();
//    bool ok = drvStandby(true);
//    i2cUnlock();
//    if (ok) {
//      g_drvStandbyState = true;
//      g_lastHapticIoMs = now;
//    }
//  }
//}
//
//static void hapticsOn(uint8_t duty) {
//  if (!g_drvOk) {
//    ledcWrite(HAP_PWM_PIN, 0);
//    g_hapticPwmActive = false;
//    return;
//  }
//
//  uint32_t now = millis();
//
//  if (g_drvStandbyState && (now - g_lastHapticIoMs >= 15)) {
//    i2cLock();
//    bool ok = drvStandby(false);
//    i2cUnlock();
//    if (ok) {
//      g_drvStandbyState = false;
//      g_lastHapticIoMs = now;
//    }
//  }
//
//  // Mute DSP UNIQUEMENT au front montant (début impulsion)
//  if (!g_hapticPwmActive) {
//    g_hapticMuteUntilMs = now + HAPTIC_DSP_MUTE_MS;
//    g_hapticPwmActive = true;
//  }
//
//  ledcWrite(HAP_PWM_PIN, duty);
//}
//
//// Loi haptique “produit” + hystérésis seuil
//static void updateHapticsFromCents(float cents) {
//  if (!g_hasPitch || (millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) {
//    hapticsHardOff();
//    return;
//  }
//
//  float a = fabsf(cents);
//  if (a > HAP_MAXCENTS) a = HAP_MAXCENTS;
//
//  static bool vibOn = false;
//  const float ON_TH  = 6.0f;
//  const float OFF_TH = 4.5f;
//
//  if (!vibOn) vibOn = (a >= ON_TH);
//  else        vibOn = (a >= OFF_TH);
//
//  if (!vibOn) { hapticsHardOff(); return; }
//
//  float x = 0.0f;
//  if (a <= HAP_DEADBAND_CENTS) x = 0.0f;
//  else x = (a - HAP_DEADBAND_CENTS) / (HAP_MAXCENTS - HAP_DEADBAND_CENTS);
//  if (x < 0) x = 0;
//  if (x > 1) x = 1;
//
//  float y = 3.0f * x * x - 2.0f * x * x * x;
//
//  const uint8_t DUTY_MIN = 35;
//  const uint8_t DUTY_MAX = 210;
//  uint8_t duty = (uint8_t)(DUTY_MIN + y * (DUTY_MAX - DUTY_MIN));
//
//  const uint32_t periodMs = 100;
//  uint32_t t = millis() % periodMs;
//
//  uint32_t onBase  = (cents > 0) ? 22 : 50;
//  uint32_t onBonus = (uint32_t)(y * 20.0f);
//  uint32_t onMs    = onBase + onBonus;
//  if (onMs > 80) onMs = 80;
//
//  if (t < onMs) hapticsOn(duty);
//  else          hapticsHardOff();
//}
//
//// ===============================
////   AUDIO utils
//// ===============================
//static float computeRMS(const float *buffer, size_t length) {
//  double sumSq = 0;
//  for (size_t i = 0; i < length; i++) sumSq += (double)buffer[i] * buffer[i];
//  return (float)sqrt(sumSq / length);
//}
//
//// Gate adaptatif dynamique (assoupli pour mieux accrocher les notes faibles)
//static bool computeAdaptiveGate(float rms, float &gateUsed) {
//  if (!g_noiseEmaInit) {
//    g_noiseEma = g_gateRms;
//    g_noiseEmaInit = true;
//  } else {
//    float silenceLimit = g_gateRms * 0.85f;
//    if (rms < silenceLimit) {
//      g_noiseEma = (1.0f - NOISE_EMA_ALPHA) * g_noiseEma + NOISE_EMA_ALPHA * rms;
//    }
//  }
//
//  float gate = g_noiseEma * 2.0f;   // avant 2.5
//  if (gate < SILENCE_RMS_THRES) gate = SILENCE_RMS_THRES;
//  if (gate > 0.015f) gate = 0.015f; // avant 0.02
//
//  gateUsed = gate;
//  return (rms >= gate);
//}
//
//static void changerProfil(Instrument p) {
//  profilActuel = p;
//  Config c = reglages[p];
//
//  float fHP = c.fMin * 0.8f;
//  float fLP = c.fMax * 1.2f;
//
//  g_hpAlpha = expf(-2.0f * 3.1415926f * fHP / SAMPLE_RATE);
//  g_lpAlpha = expf(-2.0f * 3.1415926f * fLP / SAMPLE_RATE);
//
//  g_hpY = 0; g_hpXprev = 0; g_lpY = 0;
//
//  for (int i = 0; i < NOTE_WINDOW; i++) { g_freqWin[i] = 0; g_midiWin[i] = -9999; }
//  g_winIdx = 0; g_winCount = 0; g_lockedMidi = -1;
//
//  resetDetectionState();
//  g_noiseEmaInit = false;
//  g_hapticMuteUntilMs = 0;
//  g_hapticPwmActive = false;
//
//  setInstrumentColor(p);
//  DBG_PRINTF("\n>>> Profil: %s (%.0fHz - %.0fHz)\n", c.nom, c.fMin, c.fMax);
//}
//
//static void appliquerFiltre(float *buffer, int taille) {
//  if (!g_hannInit) initHannWindow();
//
//  for (int i = 0; i < taille; i++) {
//    float x = buffer[i];
//    float yhp = g_hpAlpha * (g_hpY + x - g_hpXprev);
//    g_hpY = yhp; g_hpXprev = x;
//
//    float ylp = g_lpAlpha * g_lpY + (1.0f - g_lpAlpha) * yhp;
//    g_lpY = ylp;
//
//    buffer[i] = ylp * g_hann[i];
//  }
//}
//
//static float detectPitchYIN(const float *input, float *outYinMin = nullptr) {
//  int tauMin = (int)(SAMPLE_RATE / reglages[profilActuel].fMax);
//  int tauMax = (int)(SAMPLE_RATE / reglages[profilActuel].fMin);
//  if (tauMax >= YIN_BUFFER_SIZE - 2) tauMax = YIN_BUFFER_SIZE - 2;
//  if (tauMin < 2) tauMin = 2;
//
//  for (int tau = 0; tau <= tauMax; tau++) {
//    double sum = 0;
//    for (int i = 0; i < YIN_BUFFER_SIZE - tau; i++) {
//      float d = input[i] - input[i + tau];
//      sum += (double)d * (double)d;
//    }
//    g_yinBuffer[tau] = (float)sum;
//  }
//
//  g_yinBuffer[0] = 1.0f;
//  double runningSum = 0;
//  float yinMin = 1.0f;
//
//  for (int tau = 1; tau <= tauMax; tau++) {
//    runningSum += g_yinBuffer[tau];
//    g_yinBuffer[tau] = (float)(g_yinBuffer[tau] * tau / (float)runningSum);
//    if (tau >= tauMin && g_yinBuffer[tau] < yinMin) yinMin = g_yinBuffer[tau];
//  }
//
//  const float threshold = 0.15f;
//  int tauFound = -1;
//  for (int tau = tauMin; tau <= tauMax; tau++) {
//    if (g_yinBuffer[tau] < threshold) {
//      while (tau + 1 <= tauMax && g_yinBuffer[tau + 1] < g_yinBuffer[tau]) tau++;
//      tauFound = tau;
//      break;
//    }
//  }
//
//  if (outYinMin) *outYinMin = yinMin;
//  if (tauFound < 0) return -1.0f;
//
//  float s0 = g_yinBuffer[tauFound - 1];
//  float s1 = g_yinBuffer[tauFound];
//  float s2 = g_yinBuffer[tauFound + 1];
//
//  float denom = (2.0f * s1 - s2 - s0);
//  float delta = 0.0f;
//  if (fabsf(denom) > 1e-9f) delta = 0.5f * (s2 - s0) / denom;
//
//  float tauInterp = tauFound + delta;
//
//  for (int k = 2; k <= 4; k++) {
//    int t = (int)(tauInterp / k);
//    if (t >= tauMin && g_yinBuffer[t] < 0.20f) tauInterp /= k;
//  }
//
//  if (tauInterp <= 0.0f) return -1.0f;
//  return SAMPLE_RATE / tauInterp;
//}
//
//static int freqToMidi(float freq) {
//  float n = 12.0f * log2f(freq / 440.0f);
//  return (int)roundf(n) + 69;
//}
//
//static float midiToFreq(int midi) {
//  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
//}
//
//static void midiToNoteName(int midi, const char **note, int *oct) {
//  static const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
//  int idx = ((midi % 12) + 12) % 12;
//  *note = names[idx];
//  *oct = midi / 12 - 1;
//}
//
//static float centsAgainstMidi(float freq, int midiRef) {
//  float ref = midiToFreq(midiRef);
//  return 1200.0f * log2f(freq / ref);
//}
//
//static int dominantMidi(const int *midiArr, int n, int *outCount) {
//  int bestMidi = midiArr[0];
//  int bestCount = 1;
//  for (int i = 0; i < n; i++) {
//    int m = midiArr[i];
//    int count = 0;
//    for (int j = 0; j < n; j++) if (midiArr[j] == m) count++;
//    if (count > bestCount) { bestCount = count; bestMidi = m; }
//  }
//  if (outCount) *outCount = bestCount;
//  return bestMidi;
//}
//
//static float medianFloat(float *arr, int n) {
//  for (int i = 0; i < n - 1; i++) {
//    for (int j = i + 1; j < n; j++) {
//      if (arr[j] < arr[i]) { float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp; }
//    }
//  }
//  if (n <= 0) return 0.0f;
//  if (n % 2 == 1) return arr[n / 2];
//  return 0.5f * (arr[n/2 - 1] + arr[n/2]);
//}
//
//// ===============================
////   I2S init/read
//// ===============================
//static bool initI2S() {
//  i2s_config_t cfg = {};
//  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
//  cfg.sample_rate = (int)SAMPLE_RATE;
//  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
//  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
//  cfg.communication_format = I2S_COMM_FORMAT_I2S;
//  cfg.dma_buf_count = 8;
//  cfg.dma_buf_len = 256;
//  cfg.use_apll = false;
//  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
//
//  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
//
//  i2s_pin_config_t pins = {
//    .bck_io_num = I2S_BCK_PIN,
//    .ws_io_num  = I2S_WS_PIN,
//    .data_out_num = I2S_PIN_NO_CHANGE,
//    .data_in_num  = I2S_DATA_PIN
//  };
//  return i2s_set_pin(I2S_PORT, &pins) == ESP_OK;
//}
//
//static size_t readI2SBlock(float *buffer, size_t n) {
//  static int32_t raw[256];
//  size_t read = 0;
//  while (read < n) {
//    size_t bytes = 0;
//    i2s_read(I2S_PORT, raw, sizeof(raw), &bytes, portMAX_DELAY);
//    for (size_t i = 0; i < bytes / 4 && read < n; i++) {
//      buffer[read++] = (raw[i] >> 8) / 8388608.0f;
//    }
//  }
//  return read;
//}
//
//// ===============================
////   Battery
//// ===============================
//static bool readBatterySafe(float &vbat, float &soc) {
//  for (int attempt = 0; attempt < 5; attempt++) {
//    vbat = maxlipo.cellVoltage();
//    soc  = maxlipo.cellPercent();
//    bool okV = !isnan(vbat) && (vbat > 2.5f) && (vbat < 4.5f);
//    bool okS = !isnan(soc)  && (soc >= 0.0f) && (soc <= 100.0f);
//    if (okV && okS) return true;
//    delay(60);
//  }
//  return false;
//}
//
//// ===============================
////   Gate calibrage (boot)
//// ===============================
//static void calibrateNoiseFloor() {
//  const int frames = 20;
//  double acc = 0;
//
//  hapticsHardOff();
//
//  for (int i = 0; i < frames; i++) {
//    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
//    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
//    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
//    acc += rms;
//    vTaskDelay(pdMS_TO_TICKS(10));
//  }
//
//  g_noiseFloor = (float)(acc / frames);
//  g_gateRms = fmaxf(SILENCE_RMS_THRES, g_noiseFloor * 2.0f);
//
//  g_noiseEma = g_gateRms;
//  g_noiseEmaInit = true;
//
//  DBG_PRINTF("NoiseFloor=%.6f -> GateBase=%.6f\n", g_noiseFloor, g_gateRms);
//}
//
//// ===============================
////   TASKS
//// ===============================
//static void TaskBattery(void *pv) {
//  (void)pv;
//  for (;;) {
//    if (g_battOk) {
//      float v, s;
//      i2cLock();
//      bool ok = readBatterySafe(v, s);
//      i2cUnlock();
//      if (ok) setBatteryColor(s);
//      else    setBatteryColor(NAN);
//    } else {
//      setBatteryColor(NAN);
//    }
//    vTaskDelay(pdMS_TO_TICKS(BATT_PERIOD_MS));
//  }
//}
//
//static void TaskUI(void *pv) {
//  (void)pv;
//  int last = digitalRead(BTN_PIN);
//  uint32_t lastChange = millis();
//
//  for (;;) {
//    int now = digitalRead(BTN_PIN);
//    if (now != last) { last = now; lastChange = millis(); }
//
//    if ((millis() - lastChange) > 30) {
//      if (now == LOW) {
//        Instrument p = (Instrument)(((int)profilActuel + 1) % 5);
//        changerProfil(p);
//        while (digitalRead(BTN_PIN) == LOW) vTaskDelay(pdMS_TO_TICKS(20));
//      }
//    }
//    vTaskDelay(pdMS_TO_TICKS(10));
//  }
//}
//
//static void TaskAudio(void *pv) {
//  (void)pv;
//
//  static bool gateOpen = false;
//
//  const float ALPHA = 0.12f;
//  const float MAX_STEP_CENTS = 2.0f;
//
//  for (;;) {
//    if (!g_drvOk) {
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(20));
//      continue;
//    }
//
//    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
//    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
//
//    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
//    g_lastRms = rms;
//
//    float gateUsed = g_gateRms;
//    bool hasSignal = computeAdaptiveGate(rms, gateUsed);
//
//    const float gateOn  = gateUsed;
//    const float gateOff = gateUsed * 0.70f;
//
//    if (!gateOpen) gateOpen = hasSignal;
//    else           gateOpen = (rms >= gateOff);
//
//    if (!gateOpen) {
//      resetDetectionState();
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (millis() < (uint32_t)g_hapticMuteUntilMs) {
//      resetPitchTrackingQuick();
//      g_hasPitch = false;
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (g_lastValidDetectMs != 0 && (millis() - g_lastValidDetectMs) > 350) {
//      resetPitchTrackingQuick();
//      resetFreqSmoother();
//      g_lastAcceptedFreq = 0.0f;
//    }
//
//    float yinMin = 1.0f;
//    float f_micro = detectPitchYIN(g_audioBuffer, &yinMin);
//    g_lastYin = yinMin;
//
//    const float YIN_GOOD = reglages[profilActuel].yinGood;
//    bool pitchValid = (f_micro > 0.0f) && (yinMin > 0.0f) && (yinMin <= YIN_GOOD);
//
//    if (!pitchValid) {
//      g_statRejectYin++;
//      g_hasPitch = false;
//      if ((millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    // Validation multi-trames accélérée
//    {
//      float relTol = (profilActuel == CHROMATIQUE) ? 0.025f : 0.04f;
//      uint8_t confirmNeeded = (profilActuel == CHROMATIQUE) ? 3 : 2; // <- plus rapide
//
//      if (g_candidateCount == 0) {
//        g_candidateFreq = f_micro;
//        g_candidateCount = 1;
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//
//      if (!isCloseFreq(f_micro, g_candidateFreq, relTol)) {
//        g_candidateFreq = f_micro;
//        g_candidateCount = 1;
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//
//      g_candidateCount++;
//      g_candidateFreq = 0.7f * g_candidateFreq + 0.3f * f_micro;
//
//      if (g_candidateCount < confirmNeeded) {
//        g_hasPitch = false;
//        hapticsHardOff();
//        vTaskDelay(pdMS_TO_TICKS(5));
//        continue;
//      }
//    }
//
//    float f = g_candidateFreq;
//    g_candidateCount = 0;
//    g_lastValidDetectMs = millis();
//
//    if (profilActuel == GUITARE) {
//      f = correctOctaveForGuitar(f);
//    }
//
//    float rmsExcess = rms - g_noiseEma;
//    if (profilActuel == CHROMATIQUE &&
//        f >= MAINS_REJECT_FMIN && f <= MAINS_REJECT_FMAX &&
//        rmsExcess < MAINS_REJECT_MAX_EXCESS) {
//      g_statRejectMains++;
//      g_hasPitch = false;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    if (!isFrequencyPlausible(f, profilActuel)) {
//      g_statRejectImpl++;
//      g_hasPitch = false;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    f = smoothFrequencyRobust(f);
//
//    g_lastAcceptedFreq = f;
//    g_lastFreq = f;
//    g_hasPitch = true;
//    g_lastPitchMs = millis();
//
//    int midi = freqToMidi(f);
//    g_freqWin[g_winIdx] = f;
//    g_midiWin[g_winIdx] = midi;
//    g_winIdx = (g_winIdx + 1) % NOTE_WINDOW;
//    if (g_winCount < NOTE_WINDOW) g_winCount++;
//
//    if (g_winCount < 2) {
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    int midis[NOTE_WINDOW];
//    int n = g_winCount;
//    for (int i = 0; i < n; i++) midis[i] = g_midiWin[i];
//
//    int domCount = 0;
//    int domMidi = dominantMidi(midis, n, &domCount);
//
//    const float LOCK_RATIO = 0.6f;
//    if (g_lockedMidi < 0) g_lockedMidi = domMidi;
//    else if (domMidi != g_lockedMidi) {
//      if ((float)domCount >= LOCK_RATIO * n) g_lockedMidi = domMidi;
//    }
//
//    float centsList[NOTE_WINDOW];
//    int cN = 0;
//    for (int i = 0; i < n; i++) {
//      float ff = g_freqWin[i];
//      if (ff <= 0) continue;
//      float c = centsAgainstMidi(ff, g_lockedMidi);
//      if (fabsf(c) > 80.0f) continue;
//      centsList[cN++] = c;
//    }
//
//    if (cN < 1) {
//      g_statRejectNoPitch++;
//      hapticsHardOff();
//      vTaskDelay(pdMS_TO_TICKS(5));
//      continue;
//    }
//
//    float centsMed = medianFloat(centsList, cN);
//
//    if (!g_centsInit) {
//      g_centsSmooth = centsMed;
//      g_centsInit = true;
//    } else {
//      float target = (1.0f - ALPHA) * g_centsSmooth + ALPHA * centsMed;
//      float delta = target - g_centsSmooth;
//      if (delta >  MAX_STEP_CENTS) delta =  MAX_STEP_CENTS;
//      if (delta < -MAX_STEP_CENTS) delta = -MAX_STEP_CENTS;
//      g_centsSmooth += delta;
//    }
//
//    g_lastCents = g_centsSmooth;
//    updateHapticsFromCents(g_centsSmooth);
//
//    const char *noteName;
//    int oct;
//    midiToNoteName(g_lockedMidi, &noteName, &oct);
//    float ref = midiToFreq(g_lockedMidi);
//
//    g_statFramesOK++;
//
//    DBG_PRINTF("[%s] f=%.2fHz note=%s%d ref=%.2fHz centsMed=%+.2f centsSm=%+.2f "
//               "N=%d dom=%d/%d rms=%.5f gate=%.5f nEma=%.5f yin=%.3f\n",
//               reglages[profilActuel].nom, f, noteName, oct, ref, centsMed, g_centsSmooth,
//               cN, domCount, n, rms, gateUsed, g_noiseEma, yinMin);
//
//    uint32_t now = millis();
//    if (now - g_lastStatsMs > 5000) {
//      DBG_PRINTF("STATS ok=%lu rejYIN=%lu rejMains=%lu rejImpl=%lu rejNoPitch=%lu\n",
//                 (unsigned long)g_statFramesOK,
//                 (unsigned long)g_statRejectYin,
//                 (unsigned long)g_statRejectMains,
//                 (unsigned long)g_statRejectImpl,
//                 (unsigned long)g_statRejectNoPitch);
//      g_lastStatsMs = now;
//    }
//
//    vTaskDelay(pdMS_TO_TICKS(5));
//  }
//}
//
//// ===============================
////   SETUP
//// ===============================
//void setup() {
//  DBG_BEGIN(115200);
//  delay(150);
//
//  if (!g_hannInit) initHannWindow();
//
//  pinMode(HAP_PWM_PIN, OUTPUT);
//  digitalWrite(HAP_PWM_PIN, LOW);
//  delay(50);
//
//  pinMode(BTN_PIN, INPUT);
//
//  pwmAttach(LED_BATT_R, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_BATT_G, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_BATT_B, LED_PWM_FREQ, LED_PWM_RES);
//
//  pwmAttach(LED_INST_R, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_INST_G, LED_PWM_FREQ, LED_PWM_RES);
//  pwmAttach(LED_INST_B, LED_PWM_FREQ, LED_PWM_RES);
//
//  setBattRGB(255, 0, 0);
//
//  pwmAttach(HAP_PWM_PIN, HAP_PWM_FREQ, HAP_PWM_RES);
//  ledcWrite(HAP_PWM_PIN, 0);
//
//  g_i2cMutex = xSemaphoreCreateMutex();
//
//  Wire.begin(I2C_SDA, I2C_SCL);
//  Wire.setClock(I2C_FREQ);
//  delay(200);
//
//  changerProfil(GUITARE);
//
//  bool i2sOk = initI2S();
//  if (!i2sOk) DBG_PRINTLN("ERREUR: initI2S() a echoue");
//
//  float vbat = NAN, soc = NAN;
//  bool ok = false;
//
//  i2cLock();
//  g_battOk = maxlipo.begin();
//  if (g_battOk) {
//    maxlipo.wake();
//    maxlipo.quickStart();
//    delay(400);
//    ok = readBatterySafe(vbat, soc);
//  }
//  i2cUnlock();
//
//  if (!g_battOk || !ok) {
//    DBG_PRINTLN("BATTERIE: lecture echec.");
//    setBatteryColor(NAN);
//  } else {
//    DBG_PRINTF("BATTERIE: VBAT=%.3fV SOC=%.1f%%\n", vbat, soc);
//    setBatteryColor(soc);
//  }
//
//  bool drvOk = false;
//  i2cLock();
//  drvOk = drvInitPwmInput();
//  if (drvOk) {
//    drvDump();
//    drvOk = drvStandby(true);
//  }
//  i2cUnlock();
//
//  g_drvOk = drvOk;
//  g_drvStandbyState = true;
//  g_hapticPwmActive = false;
//  DBG_PRINTLN(drvOk ? "DRV2605L: OK + STANDBY (hard-off)" : "DRV2605L: INIT ECHEC (I2C?)");
//
//  hapticsHardOff();
//
//  if (drvOk && i2sOk) calibrateNoiseFloor();
//
//  xTaskCreatePinnedToCore(TaskAudio,   "Audio",   8192, nullptr, 3, nullptr, 1);
//  xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 2, nullptr, 1);
//  xTaskCreatePinnedToCore(TaskBattery, "Battery", 4096, nullptr, 1, nullptr, 1);
//
//  DBG_PRINTLN("OK: tasks started.");
//}
//
//void loop() {
//  vTaskDelay(pdMS_TO_TICKS(1000));
//}




#include <Arduino.h>
#include "driver/i2s.h"
#include <Wire.h>
#include "Adafruit_MAX1704X.h"
#include <math.h>

// ===============================
//   SERIAL DEBUG
// ===============================
#define DEBUG_SERIAL 1
#define BATTERY_TEST_SERIAL 1

#if DEBUG_SERIAL
  #define DBG_BEGIN(x)    Serial.begin(x)
  #define DBG_PRINT(...)  Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_BEGIN(x)
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
  #define DBG_PRINTF(...)
#endif

// ===============================
//   PROFILS INSTRUMENTS
// ===============================
enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
volatile Instrument profilActuel = GUITARE;

struct Config {
  float fMin;
  float fMax;
  const char* nom;
  float yinGood;
};

static Config reglages[] = {
  {190.0f, 700.0f,  "Violon",      0.20f},
  { 80.0f, 700.0f,  "Guitare",     0.24f},   // un peu plus permissif
  {260.0f, 700.0f,  "Ukulele",     0.20f},
  { 30.0f, 200.0f,  "Basse",       0.28f},
  { 30.0f, 2000.0f, "Chromatique", 0.20f}
};

// ===============================
//   I2C  (MODIFIÉ)
// ===============================
static const int I2C_SDA = 18;   // <-- MODIF
static const int I2C_SCL = 19;   // <-- MODIF
static const uint32_t I2C_FREQ = 50000;

static SemaphoreHandle_t g_i2cMutex = nullptr;
static inline void i2cLock()   { if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
static inline void i2cUnlock() { if (g_i2cMutex) xSemaphoreGive(g_i2cMutex); }

// ===============================
//   DRV2605L (PWM/Analog)
// ===============================
static const uint8_t DRV_ADDR = 0x5A;

static const int HAP_PWM_PIN  = 4;   // <-- MODIF (IN du driver)
static const int HAP_PWM_FREQ = 1000;
static const int HAP_PWM_RES  = 8;

static const uint8_t REG_MODE = 0x01; // bit6 = STANDBY

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

  if (!drvWriteReg(0x01, 0x03)) return false; // MODE=3 PWM/Analog
  delay(5);

  uint8_t c1 = drvReadReg(0x1A);
  if (c1 == 0xFF) return false;
  c1 &= (uint8_t)~(1 << 7); // ERM
  if (!drvWriteReg(0x1A, c1)) return false;

  uint8_t c3 = drvReadReg(0x1D);
  if (c3 == 0xFF) return false;
  c3 |=  (uint8_t)(1 << 5); // ERM_OPEN_LOOP
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

// ===============================
//   BATTERY (MAX17048)
// ===============================
Adafruit_MAX17048 maxlipo;
static bool g_battOk = false;

static const float ORANGE_PCT = 30.0f;
static const float RED_PCT    = 10.0f;
static const uint32_t BATT_PERIOD_MS = 60000;

static const int LED_BATT_R = 27;
static const int LED_BATT_G = 12;
static const int LED_BATT_B = 14;

// --- Simulation batterie (ajoutée) ---
#if BATTERY_TEST_SERIAL
static volatile bool  g_battSimMode = false;        // false=reel, true=simulation
static volatile float g_battSimSoc  = 50.0f;        // %
static volatile float g_battSimVbat = 3.90f;        // V
static volatile bool  g_battForceRefresh = false;
#endif

// ===============================
//   LED INSTRUMENT
// ===============================
static const int LED_INST_R = 32;
static const int LED_INST_G = 13;
static const int LED_INST_B = 33;

static const int BTN_PIN = 34;

// ===============================
//   LEDC
// ===============================
static const int LED_PWM_FREQ = 5000;
static const int LED_PWM_RES  = 8;

static inline void pwmAttach(int pin, int freq, int res) {
  ledcAttach(pin, freq, res);
  ledcWrite(pin, 0);
}

// ===============================
//   LED helpers
// ===============================
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

// ===============================
//   AUDIO I2S
// ===============================
static const i2s_port_t I2S_PORT   = I2S_NUM_0;
static const int I2S_BCK_PIN       = 5;
static const int I2S_WS_PIN        = 25;
static const int I2S_DATA_PIN      = 35;

static const float SAMPLE_RATE     = 20000.0f;
static const int   YIN_BUFFER_SIZE = 1024;

static float g_audioBuffer[YIN_BUFFER_SIZE];
static float g_yinBuffer[YIN_BUFFER_SIZE];
static float g_hann[YIN_BUFFER_SIZE];
static bool  g_hannInit = false;

// filtres HP/LP
static float g_hpAlpha, g_lpAlpha;
static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;

// Gate adaptatif
static const float SILENCE_RMS_THRES = 0.0010f; // un peu plus sensible
static float g_noiseFloor = 0.0f;
static float g_gateRms    = SILENCE_RMS_THRES;
static bool  g_noiseEmaInit = false;
static float g_noiseEma     = 0.0f;
static const float NOISE_EMA_ALPHA = 0.02f;

// ===============================
//   PARTAGE ENTRE TASKS
// ===============================
static volatile float g_lastFreq  = -1.0f;
static volatile float g_lastCents = 0.0f;
static volatile float g_lastRms   = 0.0f;
static volatile float g_lastYin   = 1.0f;
static volatile bool  g_hasPitch  = false;

// ===============================
//   Cents smoothing + slew limiter
// ===============================
static float g_centsSmooth = 0.0f;
static bool  g_centsInit   = false;

// ===============================
//   Pitch tracking robustesse
// ===============================
static float    g_candidateFreq = 0.0f;
static uint8_t  g_candidateCount = 0;
static uint32_t g_lastValidDetectMs = 0;
static float    g_lastAcceptedFreq = 0.0f;

static float g_fPrev1 = 0.0f, g_fPrev2 = 0.0f;
static bool  g_havePrev1 = false, g_havePrev2 = false;
static float g_fEma = 0.0f;
static bool  g_fEmaInit = false;
static const float FREQ_EMA_ALPHA = 0.35f;

// ===============================
//   HAPTICS
// ===============================
static volatile bool     g_drvOk = false;
static volatile uint32_t g_lastPitchMs = 0;
static const uint32_t    PITCH_TIMEOUT_MS = 250;

static const float HAP_DEADBAND_CENTS = 5.0f;
static const float HAP_MAXCENTS       = 50.0f;

// Mute DSP après vibration (réduit pour meilleure réactivité)
static volatile uint32_t g_hapticMuteUntilMs = 0;
static const uint32_t HAPTIC_DSP_MUTE_MS = 40;

static uint32_t g_lastHapticIoMs = 0;
static bool g_drvStandbyState = true;

// anti 50 Hz
static const float MAINS_REJECT_FMIN = 47.0f;
static const float MAINS_REJECT_FMAX = 53.0f;
static const float MAINS_REJECT_MAX_EXCESS = 0.0030f;

// cordes guitare
static const float guitarStringsHz[] = {
  82.4069f, 110.000f, 146.832f, 195.998f, 246.942f, 329.628f
};

// ===============================
//   NOTE DOMINANTE + MEDIANE
// ===============================
static const int NOTE_WINDOW = 5;
static float g_freqWin[NOTE_WINDOW];
static int   g_midiWin[NOTE_WINDOW];
static int   g_winIdx = 0;
static int   g_winCount = 0;
static int   g_lockedMidi = -1;

// ===============================
//   STATS
// ===============================
static uint32_t g_statRejectNoPitch = 0;
static uint32_t g_statRejectYin     = 0;
static uint32_t g_statRejectMains   = 0;
static uint32_t g_statRejectImpl    = 0;
static uint32_t g_statFramesOK      = 0;
static uint32_t g_lastStatsMs       = 0;

// ===============================
//   Helpers
// ===============================
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
  if (p == GUITARE) return ratio <= 2.4f;
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

// ===============================
//   Batterie : simulation (série)
// ===============================
#if BATTERY_TEST_SERIAL
static void batteryRequestRefresh() { g_battForceRefresh = true; }

static void printBatteryHelp() {
  DBG_PRINTLN("\n[BAT] Commandes:");
  DBG_PRINTLN("  bat help");
  DBG_PRINTLN("  bat sim");
  DBG_PRINTLN("  bat real");
  DBG_PRINTLN("  bat soc <0..100>");
  DBG_PRINTLN("  bat vbat <2.5..4.5>");
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
    batteryRequestRefresh();
    DBG_PRINTLN("[BAT] Mode simulation actif");
    return;
  }

  if (lower == "bat real") {
    g_battSimMode = false;
    batteryRequestRefresh();
    DBG_PRINTLN("[BAT] Mode reel actif");
    return;
  }

  if (lower.startsWith("bat soc ")) {
    float val = lower.substring(8).toFloat();
    if (val < 0.0f) val = 0.0f;
    if (val > 100.0f) val = 100.0f;
    g_battSimMode = true;
    g_battSimSoc = val;
    batteryRequestRefresh();
    DBG_PRINTF("[BAT] SOC simule=%.1f%%\n", val);
    return;
  }

  if (lower.startsWith("bat vbat ")) {
    float val = lower.substring(9).toFloat();
    if (val < 2.5f) val = 2.5f;
    if (val > 4.5f) val = 4.5f;
    g_battSimMode = true;
    g_battSimVbat = val;
    batteryRequestRefresh();
    DBG_PRINTF("[BAT] VBAT simule=%.3fV\n", val);
    return;
  }

  if (lower == "bat show") {
    DBG_PRINTF("[BAT] mode=%s simVBAT=%.3f simSOC=%.1f\n",
               g_battSimMode ? "SIM" : "REAL",
               g_battSimVbat, g_battSimSoc);
    return;
  }

  DBG_PRINTLN("[BAT] Commande inconnue (bat help)");
}
#endif

// ===============================
//   Haptics helpers
// ===============================
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

// === Duty linéaire (comme tu voulais) ===
static void updateHapticsFromCents(float cents) {
  if (!g_hasPitch || (millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) {
    hapticsHardOff();
    return;
  }

  float a = fabsf(cents);
  if (a > HAP_MAXCENTS) a = HAP_MAXCENTS;

  static bool vibOn = false;
  const float ON_TH  = 6.0f;
  const float OFF_TH = 4.5f;

  if (!vibOn) vibOn = (a >= ON_TH);
  else        vibOn = (a >= OFF_TH);

  if (!vibOn) { hapticsHardOff(); return; }

  float x = 0.0f;
  if (a <= HAP_DEADBAND_CENTS) x = 0.0f;
  else x = (a - HAP_DEADBAND_CENTS) / (HAP_MAXCENTS - HAP_DEADBAND_CENTS);

  if (x < 0) x = 0;
  if (x > 1) x = 1;

  const uint8_t DUTY_MIN = 30;   // plus facile à sentir
  const uint8_t DUTY_MAX = 220;

  // linéaire
  uint8_t duty = (uint8_t)(DUTY_MIN + x * (DUTY_MAX - DUTY_MIN));

  // motif temporel conservé
  const uint32_t periodMs = 100;
  uint32_t t = millis() % periodMs;

  uint32_t onBase  = (cents > 0) ? 22 : 50; // + court / - long
  uint32_t onBonus = (uint32_t)(x * 20.0f);
  uint32_t onMs    = onBase + onBonus;
  if (onMs > 80) onMs = 80;

  if (t < onMs) hapticsOn(duty);
  else          hapticsHardOff();
}

// ===============================
//   AUDIO utils
// ===============================
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

  float gate = g_noiseEma * 2.2f;
  if (gate < SILENCE_RMS_THRES) gate = SILENCE_RMS_THRES;
  if (gate > 0.02f) gate = 0.02f;

  gateUsed = gate;
  return (rms >= gate);
}

static void changerProfil(Instrument p) {
  profilActuel = p;
  Config c = reglages[p];

  float fHP = c.fMin * 0.8f;
  float fLP = c.fMax * 1.2f;

  g_hpAlpha = expf(-2.0f * 3.1415926f * fHP / SAMPLE_RATE);
  g_lpAlpha = expf(-2.0f * 3.1415926f * fLP / SAMPLE_RATE);

  g_hpY = 0; g_hpXprev = 0; g_lpY = 0;

  for (int i = 0; i < NOTE_WINDOW; i++) { g_freqWin[i] = 0; g_midiWin[i] = -9999; }
  g_winIdx = 0; g_winCount = 0; g_lockedMidi = -1;

  resetDetectionState();
  g_noiseEmaInit = false;
  g_hapticMuteUntilMs = 0;

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

// ===============================
//   I2S init/read
// ===============================
static bool initI2S() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = (int)SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num  = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_DATA_PIN
  };
  return i2s_set_pin(I2S_PORT, &pins) == ESP_OK;
}

static size_t readI2SBlock(float *buffer, size_t n) {
  static int32_t raw[256];
  size_t read = 0;
  while (read < n) {
    size_t bytes = 0;
    i2s_read(I2S_PORT, raw, sizeof(raw), &bytes, portMAX_DELAY);
    for (size_t i = 0; i < bytes / 4 && read < n; i++) {
      buffer[read++] = (raw[i] >> 8) / 8388608.0f;
    }
  }
  return read;
}

// ===============================
//   Battery
// ===============================
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

// ===============================
//   Gate calibrage
// ===============================
static void calibrateNoiseFloor() {
  const int frames = 20;
  double acc = 0;

  hapticsHardOff();

  for (int i = 0; i < frames; i++) {
    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    acc += rms;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  g_noiseFloor = (float)(acc / frames);
  g_gateRms = fmaxf(SILENCE_RMS_THRES, g_noiseFloor * 2.0f);

  g_noiseEma = g_gateRms;
  g_noiseEmaInit = true;

  DBG_PRINTF("NoiseFloor=%.6f -> GateBase=%.6f\n", g_noiseFloor, g_gateRms);
}

// ===============================
//   TASKS
// ===============================
static void TaskBattery(void *pv) {
  (void)pv;

  uint32_t nextReadMs = millis();

  for (;;) {
    bool doRead = ((int32_t)(millis() - nextReadMs) >= 0);

    #if BATTERY_TEST_SERIAL
    if (g_battForceRefresh) {
      g_battForceRefresh = false;
      doRead = true;
    }
    #endif

    if (doRead) {
      float v = NAN, s = NAN;
      bool ok = false;

      #if BATTERY_TEST_SERIAL
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
          #if BATTERY_TEST_SERIAL
            g_battSimMode ? "SIM" : "REAL",
          #else
            "REAL",
          #endif
          v, s
        );
      } else {
        setBatteryColor(NAN);
        DBG_PRINTLN("[BAT] lecture echec");
      }

      nextReadMs = millis() + BATT_PERIOD_MS;
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
    if (!g_drvOk) {
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);

    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    g_lastRms = rms;

    float gateUsed = g_gateRms;
    bool hasSignal = computeAdaptiveGate(rms, gateUsed);

    const float gateOn  = gateUsed;
    const float gateOff = gateUsed * 0.70f;

    if (!gateOpen) gateOpen = hasSignal;
    else           gateOpen = (rms >= gateOff);

    if (!gateOpen) {
      resetDetectionState();
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (millis() < (uint32_t)g_hapticMuteUntilMs) {
      resetPitchTrackingQuick();
      g_hasPitch = false;
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (g_lastValidDetectMs != 0 && (millis() - g_lastValidDetectMs) > 300) {
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
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    // Validation multi-trames (plus rapide sur guitare)
    {
      float relTol = (profilActuel == CHROMATIQUE) ? 0.025f : 0.05f;
      uint8_t confirmNeeded =
        (profilActuel == GUITARE) ? 1 :
        (profilActuel == CHROMATIQUE ? 3 : 2);

      if (g_candidateCount == 0) {
        g_candidateFreq = f_micro;
        g_candidateCount = 1;

        if (confirmNeeded > 1) {
          g_hasPitch = false;
          hapticsHardOff();
          vTaskDelay(pdMS_TO_TICKS(1));
          continue;
        }
      } else {
        if (!isCloseFreq(f_micro, g_candidateFreq, relTol)) {
          g_candidateFreq = f_micro;
          g_candidateCount = 1;

          if (confirmNeeded > 1) {
            g_hasPitch = false;
            hapticsHardOff();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
          }
        } else {
          g_candidateCount++;
          g_candidateFreq = 0.7f * g_candidateFreq + 0.3f * f_micro;

          if (g_candidateCount < confirmNeeded) {
            g_hasPitch = false;
            hapticsHardOff();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
          }
        }
      }
    }

    float f = g_candidateFreq;
    g_candidateCount = 0;
    g_lastValidDetectMs = millis();

    if (profilActuel == GUITARE) {
      f = correctOctaveForGuitar(f);
    }

    float rmsExcess = rms - g_noiseEma;
    if (profilActuel == CHROMATIQUE &&
        f >= MAINS_REJECT_FMIN && f <= MAINS_REJECT_FMAX &&
        rmsExcess < MAINS_REJECT_MAX_EXCESS) {
      g_statRejectMains++;
      g_hasPitch = false;
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (!isFrequencyPlausible(f, profilActuel)) {
      g_statRejectImpl++;
      g_hasPitch = false;
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    f = smoothFrequencyRobust(f);

    g_lastAcceptedFreq = f;
    g_lastFreq = f;
    g_hasPitch = true;
    g_lastPitchMs = millis();

    int midi = freqToMidi(f);
    g_freqWin[g_winIdx] = f;
    g_midiWin[g_winIdx] = midi;
    g_winIdx = (g_winIdx + 1) % NOTE_WINDOW;
    if (g_winCount < NOTE_WINDOW) g_winCount++;

    if (g_winCount < 2) {
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    int midis[NOTE_WINDOW];
    int n = g_winCount;
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
      g_statRejectNoPitch++;
      hapticsHardOff();
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
      DBG_PRINTF("STATS ok=%lu rejYIN=%lu rejMains=%lu rejImpl=%lu rejNoPitch=%lu\n",
                 (unsigned long)g_statFramesOK,
                 (unsigned long)g_statRejectYin,
                 (unsigned long)g_statRejectMains,
                 (unsigned long)g_statRejectImpl,
                 (unsigned long)g_statRejectNoPitch);
      g_lastStatsMs = now;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ===============================
//   SETUP
// ===============================
void setup() {
  DBG_BEGIN(115200);
  delay(150);

  if (!g_hannInit) initHannWindow();

  pinMode(HAP_PWM_PIN, OUTPUT);
  digitalWrite(HAP_PWM_PIN, LOW);
  delay(50);

  pinMode(BTN_PIN, INPUT);

  pwmAttach(LED_BATT_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_B, LED_PWM_FREQ, LED_PWM_RES);

  pwmAttach(LED_INST_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_B, LED_PWM_FREQ, LED_PWM_RES);

  setBattRGB(255, 0, 0);

  pwmAttach(HAP_PWM_PIN, HAP_PWM_FREQ, HAP_PWM_RES);
  ledcWrite(HAP_PWM_PIN, 0);

  g_i2cMutex = xSemaphoreCreateMutex();

  Wire.begin(I2C_SDA, I2C_SCL);     // <-- GPIO18 / GPIO19
  Wire.setClock(I2C_FREQ);
  delay(200);

  changerProfil(GUITARE);

  bool i2sOk = initI2S();
  if (!i2sOk) DBG_PRINTLN("ERREUR: initI2S() a echoue");

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
    DBG_PRINTLN("BATTERIE: lecture echec.");
    setBatteryColor(NAN);
  } else {
    DBG_PRINTF("BATTERIE: VBAT=%.3fV SOC=%.1f%%\n", vbat, soc);
    setBatteryColor(soc);
  }

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
  DBG_PRINTLN(drvOk ? "DRV2605L: OK + STANDBY (hard-off)" : "DRV2605L: INIT ECHEC (I2C?)");

  hapticsHardOff();

  if (drvOk && i2sOk) calibrateNoiseFloor();

  #if BATTERY_TEST_SERIAL
    printBatteryHelp();
  #endif

  xTaskCreatePinnedToCore(TaskAudio,   "Audio",   8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(TaskBattery, "Battery", 4096, nullptr, 1, nullptr, 1);

  DBG_PRINTLN("OK: tasks started.");
}

void loop() {
  #if BATTERY_TEST_SERIAL
    handleBatterySerialCommands();
  #endif

  vTaskDelay(pdMS_TO_TICKS(20));
}
