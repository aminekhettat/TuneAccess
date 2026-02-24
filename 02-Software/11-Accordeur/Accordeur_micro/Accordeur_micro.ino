#include <Arduino.h>
#include "driver/i2s.h"
#include <Wire.h>
#include "Adafruit_MAX1704X.h"
#include <math.h>

// ===============================
//   SERIAL DEBUG
// ===============================
#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
  #define DBG_BEGIN(x)    Serial.begin(x)
  #define DBG_PRINTLN(x)  Serial.println(x)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_BEGIN(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(...)
#endif

// ===============================
//   PROFILS INSTRUMENTS
// ===============================
enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
volatile Instrument profilActuel = GUITARE;

struct Config { float fMin; float fMax; const char* nom; };
static Config reglages[] = {
  {190.0f, 700.0f,  "Violon"},
  {80.0f,  400.0f,  "Guitare"},
  {260.0f, 500.0f,  "Ukulele"},
  {30.0f,  150.0f,  "Basse"},
  {30.0f,  2000.0f, "Chromatique"}
};

// ===============================
//   I2C
// ===============================
static const int I2C_SDA = 18; //21;
static const int I2C_SCL = 19; //22;
static const uint32_t I2C_FREQ = 50000;

static SemaphoreHandle_t g_i2cMutex = nullptr;
static inline void i2cLock()   { xSemaphoreTake(g_i2cMutex, portMAX_DELAY); }
static inline void i2cUnlock() { xSemaphoreGive(g_i2cMutex); }

// ===============================
//   DRV2605L
// ===============================
static const uint8_t DRV_ADDR = 0x5A;

// IN pin (PWM/Analog mode) -> IO26
static const int HAP_PWM_PIN  = 26;
// Conseil : éviter 200 Hz (bande audio). 1000 Hz est souvent plus sain.
static const int HAP_PWM_FREQ = 1000;
static const int HAP_PWM_RES  = 8;

// DRV registers
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

// Mode PWM/Analog input (IN = PWM), ERM, open-loop
static bool drvInitPwmInput() {
  if (!drvWriteReg(0x01, 0x80)) return false; // reset
  delay(10);

  if (!drvWriteReg(0x01, 0x03)) return false; // MODE=3 PWM/Analog
  delay(5);

  // ERM (bit7=0) in FEEDBACK_CTRL (0x1A)
  uint8_t c1 = drvReadReg(0x1A);
  if (c1 == 0xFF) return false;
  c1 &= (uint8_t)~(1 << 7);
  if (!drvWriteReg(0x1A, c1)) return false;

  // CONTROL3 (0x1D): ERM_OPEN_LOOP=1 (bit5), PWM input (bit1=0)
  uint8_t c3 = drvReadReg(0x1D);
  if (c3 == 0xFF) return false;
  c3 |=  (uint8_t)(1 << 5);
  c3 &= (uint8_t)~(1 << 1);
  if (!drvWriteReg(0x1D, c3)) return false;

  // OD_CLAMP
  if (!drvWriteReg(0x17, 0x8C)) return false;

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
    case GUITARE:     setInstRGB(0, 255, 0);   break;
    case UKULELE:     setInstRGB(0, 255, 255); break;
    case BASSE:       setInstRGB(255, 0, 0);   break;
    case VIOLON:      setInstRGB(255, 0, 255); break;
    case CHROMATIQUE: setInstRGB(0, 0, 255);   break;
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
static const int   YIN_BUFFER_SIZE = 2048;

static float g_audioBuffer[YIN_BUFFER_SIZE];
static float g_yinBuffer[YIN_BUFFER_SIZE];

// filtres HP/LP
static float g_hpAlpha, g_lpAlpha;
static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;

// anti-jitter freq
static const int FREQ_HISTORY_SIZE = 5;
static float g_freqHistory[FREQ_HISTORY_SIZE];
static int   g_freqIndex = 0;
static bool  g_historyFilled = false;

// Gate adaptatif
static const float SILENCE_RMS_THRES = 0.0012f;
static float g_noiseFloor = 0.0f;
static float g_gateRms    = SILENCE_RMS_THRES;

// ===============================
//   PARTAGE ENTRE TASKS
// ===============================
static volatile float g_lastFreq  = -1.0f;
static volatile float g_lastCents = 0.0f;
static volatile float g_lastRms   = 0.0f;
static volatile float g_lastYin   = 1.0f;
static volatile bool  g_hasPitch  = false;

// ===============================
//   HAPTICS: HARD-OFF GARANTI
// ===============================
static volatile bool     g_drvOk = false;
static volatile uint32_t g_lastPitchMs = 0;

static const float    HAP_DEADBAND_CENTS = 5.0f;     // vibre seulement si |cents| > 5
static const float    HAP_MAXCENTS       = 25.0f;
static const uint32_t PITCH_TIMEOUT_MS   = 250;      // si plus de pitch -> OFF

static inline uint8_t pctToDuty(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (uint8_t)((pct * 255) / 100);
}

// OFF = duty 0 + DRV STANDBY (hard-off)
static void hapticsHardOff() {
  ledcWrite(HAP_PWM_PIN, 0);
  if (g_drvOk) {
    i2cLock();
    drvStandby(true);
    i2cUnlock();
  }
}

// ON = sortir du standby puis PWM duty
static void hapticsOn(uint8_t duty) {
  if (!g_drvOk) { ledcWrite(HAP_PWM_PIN, 0); return; }
  i2cLock();
  drvStandby(false);
  i2cUnlock();
  ledcWrite(HAP_PWM_PIN, duty);
}

// ===============================
//   AUDIO utils
// ===============================
static float computeRMS(const float *buffer, size_t length) {
  double sumSq = 0;
  for (size_t i = 0; i < length; i++) sumSq += (double)buffer[i] * buffer[i];
  return (float)sqrt(sumSq / length);
}

static void changerProfil(Instrument p) {
  profilActuel = p;
  Config c = reglages[p];

  float fHP = c.fMin * 0.8f;
  float fLP = c.fMax * 1.2f;

  g_hpAlpha = expf(-2.0f * 3.1415926f * fHP / SAMPLE_RATE);
  g_lpAlpha = expf(-2.0f * 3.1415926f * fLP / SAMPLE_RATE);

  g_hpY = 0; g_hpXprev = 0; g_lpY = 0;
  for (int i = 0; i < FREQ_HISTORY_SIZE; i++) g_freqHistory[i] = 0;
  g_freqIndex = 0;
  g_historyFilled = false;

  setInstrumentColor(p);
  DBG_PRINTF("\n>>> Profil: %s (%.0fHz - %.0fHz)\n", c.nom, c.fMin, c.fMax);
}

static void appliquerFiltre(float *buffer, int taille) {
  for (int i = 0; i < taille; i++) {
    float x = buffer[i];
    float yhp = g_hpAlpha * (g_hpY + x - g_hpXprev);
    g_hpY = yhp; g_hpXprev = x;

    float ylp = g_lpAlpha * g_lpY + (1.0f - g_lpAlpha) * yhp;
    g_lpY = ylp;

    buffer[i] = ylp;
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

  return SAMPLE_RATE / tauInterp;
}

static float smoothFrequency(float f) {
  g_freqHistory[g_freqIndex++] = f;
  if (g_freqIndex >= FREQ_HISTORY_SIZE) { g_freqIndex = 0; g_historyFilled = true; }
  int n = g_historyFilled ? FREQ_HISTORY_SIZE : g_freqIndex;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += g_freqHistory[i];
  return sum / (float)n;
}

static void frequencyToNote(float freq, const char **note, int *oct, float *cents, float *nearest) {
  static const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  float n = 12.0f * log2f(freq / 440.0f);
  int midi = (int)roundf(n) + 69;

  int idx = ((midi % 12) + 12) % 12;
  *note = names[idx];
  *oct = midi / 12 - 1;

  *nearest = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
  *cents = 1200.0f * log2f(freq / *nearest);
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
//   Gate adaptatif
// ===============================
static void calibrateNoiseFloor() {
  const int frames = 20;
  double acc = 0;

  // hard-off garanti pendant calibrage
  hapticsHardOff();

  for (int i = 0; i < frames; i++) {
    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);
    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    acc += rms;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  g_noiseFloor = (float)(acc / frames);
  g_gateRms = fmaxf(SILENCE_RMS_THRES, g_noiseFloor * 3.0f);
  DBG_PRINTF("NoiseFloor=%.6f -> GateRMS=%.6f\n", g_noiseFloor, g_gateRms);
}

// ===============================
//   TASKS
// ===============================
static void TaskBattery(void *pv) {
  (void)pv;
  for (;;) {
    if (g_battOk) {
      float v, s;
      i2cLock();
      bool ok = readBatterySafe(v, s);
      i2cUnlock();
      if (ok) setBatteryColor(s);
      else    setBatteryColor(NAN);
    } else {
      setBatteryColor(NAN);
    }
    vTaskDelay(pdMS_TO_TICKS(BATT_PERIOD_MS));
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

// Audio + décision haptique
static void TaskAudio(void *pv) {
  (void)pv;

  static bool gateOpen = false;
  static int  validCount = 0;

  const float YIN_GOOD = 0.15f;

  for (;;) {
    // sécurité : si on n'a pas DRV OK => hard-off permanent
    if (!g_drvOk) {
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);

    float rms = computeRMS(g_audioBuffer, YIN_BUFFER_SIZE);
    g_lastRms = rms;

    // Gate hystérésis
    const float gateOn  = g_gateRms;
    const float gateOff = g_gateRms * 0.70f;

    if (!gateOpen) gateOpen = (rms >= gateOn);
    else           gateOpen = (rms >= gateOff);

    if (!gateOpen) {
      validCount = 0;
      g_hasPitch = false;
      hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    float yinMin = 1.0f;
    float f_micro = detectPitchYIN(g_audioBuffer, &yinMin);
    g_lastYin = yinMin;

    bool pitchValid = (f_micro > 0.0f) && (yinMin > 0.0f) && (yinMin <= YIN_GOOD);

    if (!pitchValid) {
      validCount = 0;
      g_hasPitch = false;
      // si ça fait longtemps qu'on n'a plus de pitch => hard-off
      if ((millis() - (uint32_t)g_lastPitchMs) > PITCH_TIMEOUT_MS) hapticsHardOff();
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    validCount++;
    if (validCount < 2) {
      g_hasPitch = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    float f_lisse = smoothFrequency(f_micro);
    g_lastFreq = f_lisse;
    g_hasPitch = true;
    g_lastPitchMs = millis();

    const char *note;
    int oct;
    float cents, ref;
    frequencyToNote(f_lisse, &note, &oct, &cents, &ref);
    g_lastCents = cents;

    // Décision : vibre seulement si |cents| > 5
    float a = fabsf(cents);
    if (a <= HAP_DEADBAND_CENTS) {
      hapticsHardOff();
    } else {
      if (a > HAP_MAXCENTS) a = HAP_MAXCENTS;
      int intensityPct = (int)((a - HAP_DEADBAND_CENTS) * 100.0f / (HAP_MAXCENTS - HAP_DEADBAND_CENTS));
      uint8_t duty = pctToDuty(intensityPct);

      // Pattern directionnel (pulses) : haut court / bas long
      const uint32_t periodMs = 100;
      uint32_t t = millis() % periodMs;
      uint32_t onMs = (cents > 0) ? 25 : 60;

      if (t < onMs) hapticsOn(duty);
      else          hapticsHardOff();
    }

    DBG_PRINTF("[%s] f=%.2fHz note=%s%d ref=%.2fHz cents=%+.2f rms=%.4f yin=%.3f gate=%.6f\n",
               reglages[profilActuel].nom, f_lisse, note, oct, ref, cents, rms, yinMin, g_gateRms);

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ===============================
//   SETUP
// ===============================
void setup() {
  DBG_BEGIN(115200);
  delay(150);

  // 1) GARANTIE: IO26 à 0V dès le boot (évite IN flottant)
  pinMode(HAP_PWM_PIN, OUTPUT);
  digitalWrite(HAP_PWM_PIN, LOW);
  delay(50);

  pinMode(BTN_PIN, INPUT);

  // PWM LEDs
  pwmAttach(LED_BATT_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_BATT_B, LED_PWM_FREQ, LED_PWM_RES);

  pwmAttach(LED_INST_R, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_G, LED_PWM_FREQ, LED_PWM_RES);
  pwmAttach(LED_INST_B, LED_PWM_FREQ, LED_PWM_RES);

  setBattRGB(255, 0, 0);

  // PWM haptics (duty 0)
  pwmAttach(HAP_PWM_PIN, HAP_PWM_FREQ, HAP_PWM_RES);
  ledcWrite(HAP_PWM_PIN, 0);

  // mutex I2C
  g_i2cMutex = xSemaphoreCreateMutex();

  // I2C init
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);
  delay(200);

  // Profil guitare
  changerProfil(GUITARE);

  // Init I2S
  if (!initI2S()) {
    DBG_PRINTLN("ERREUR: initI2S() a echoue");
  }

  // Battery init
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

  // DRV init + HARD OFF DEFAULT
  bool drvOk = false;
  i2cLock();
  drvOk = drvInitPwmInput();
  if (drvOk) {
    drvDump();
    drvOk = drvStandby(true); // HARD OFF par défaut
  }
  i2cUnlock();

  g_drvOk = drvOk;
  DBG_PRINTLN(drvOk ? "DRV2605L: OK + STANDBY (hard-off)" : "DRV2605L: INIT ECHEC (I2C?)");

  // Toujours hard-off au boot
  hapticsHardOff();

  // Calibrage gate (moteur OFF)
  if (drvOk && initI2S()) calibrateNoiseFloor();

  // Tasks
  xTaskCreatePinnedToCore(TaskAudio,   "Audio",   8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(TaskUI,      "UI",      4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(TaskBattery, "Battery", 4096, nullptr, 1, nullptr, 1);

  DBG_PRINTLN("OK: tasks started.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
