#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include "Adafruit_MAX1704X.h"

extern "C" {
  #include "driver/adc.h"
  #include "esp_adc/adc_continuous.h"
}

// ============================================================
// OPTIONS
// ============================================================
#define DEBUG_SERIAL 1
#define ENABLE_BATTERY_TASK 1
#define BATTERY_TEST_SERIAL 1

// ============================================================
// DEBUG
// ============================================================
#if DEBUG_SERIAL
  #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
  #define DBG_PRINTF(...)
#endif

// ============================================================
// HARD CONFIG ADC
// ============================================================
static const int ADC_PIN = 35;
static const adc_channel_t ADC_CH = ADC_CHANNEL_7; // GPIO35 = ADC1_CH7

volatile uint32_t gSampleRateHz = 20000;
volatile float gFs = 20000.0f;

static const uint16_t MAX_N = 2048;

adc_continuous_handle_t adc_handle = NULL;
static const uint32_t ADC_READ_LEN = 1024;
uint8_t adc_dma_buffer[ADC_READ_LEN];

// ============================================================
// BOUTON
// ============================================================
static const int BTN_PIN = 34;
volatile bool btnIrqFlag = false;
volatile uint32_t btnIrqCount = 0;
uint32_t lastBtnHandledMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;

// ============================================================
// LED RGB INSTRUMENT (dédiée)
// ============================================================
static const int LED_INST_R = 32;
static const int LED_INST_G = 13;
static const int LED_INST_B = 33;

// ============================================================
// LED RGB BATTERIE (dédiée)
// ============================================================
static const int LED_BAT_R = 27;
static const int LED_BAT_G = 12;
static const int LED_BAT_B = 14;

// PWM (ESP32)
static const int PWM_FREQ = 5000;
static const int PWM_RES  = 8;

// Canaux PWM LED instrument
static const int CH_INST_R = 0;
static const int CH_INST_G = 1;
static const int CH_INST_B = 2;

// Canaux PWM LED batterie
static const int CH_BAT_R  = 3;
static const int CH_BAT_G  = 4;
static const int CH_BAT_B  = 5;

// Couleurs instrument
const uint8_t instrumentColors[][3] = {
  {  0, 255,   0},   // Guitare
  {  0,   0, 255},   // Violon
  {255, 255,   0},   // Ukulele
  {255,  80,   0},   // Basse
  {255,   0, 255}    // Chromatique
};

// ============================================================
// BATTERIE (MAX17048)
// ============================================================
Adafruit_MAX17048 maxlipo;
TaskHandle_t taskBatteryHandle = nullptr;

enum BatteryLevel : uint8_t {
  BATTERY_OK = 0,
  BATTERY_LOW,
  BATTERY_CRITICAL,
  BATTERY_ERROR
};

volatile bool  batteryValid = false;
volatile float batteryVoltage = 0.0f;
volatile float batterySoc = 0.0f;
volatile BatteryLevel batteryLevel = BATTERY_ERROR;

// Période de lecture batterie : 60 s
static const uint32_t BATTERY_PERIOD_MS = 60000;
// Granularité de réveil task batterie (pour force refresh)
static const uint32_t BATTERY_TASK_TICK_MS = 100;

// Seuils (%)
static const float BAT_ORANGE_PCT = 30.0f;
static const float BAT_RED_PCT    = 10.0f;

#if BATTERY_TEST_SERIAL
volatile bool  batterySimMode = false;      // false = capteur réel, true = simulation
volatile float batterySimSoc = 50.0f;       // % simulé
volatile float batterySimVoltage = 3.90f;   // V simulée
volatile bool  batteryForceRefresh = false; // demande maj immédiate
#endif

// ============================================================
// HAPTIQUE (DRV2605L + VM390)
// ============================================================
static const int HAPTIC_SDA_PIN = 18;
static const int HAPTIC_SCL_PIN = 19;
static const uint8_t DRV_ADDR   = 0x5A;
static const uint8_t FX_PULSE = 47;
static const float HAPTIC_TUNE_TOL_CENTS = 5.0f;

// Données DSP -> Haptique
volatile bool     hapticNoteValid = false;
volatile float    hapticLastCents = 0.0f;
volatile uint32_t hapticLastNoteMs = 0;

// Mute DSP après vibration pour éviter pollution mécanique/alim
volatile uint32_t hapticMuteUntilMs = 0;

enum HapticMode : uint8_t {
  HAPTIC_IDLE = 0,
  HAPTIC_TOO_LOW,
  HAPTIC_TOO_HIGH,
  HAPTIC_IN_TUNE
};

HapticMode hapticMode = HAPTIC_IDLE;
uint32_t hapticNextTriggerMs = 0;
bool hapticTuneAckSent = false;
volatile bool hapticDrvOk = false;
uint8_t hapticSeqLoaded = 0;
uint32_t drvLastGoMs = 0;

// ============================================================
// PROFILS INSTRUMENT
// ============================================================
struct InstrumentProfile {
  const char* name;
  float fmin;
  float fmax;
  uint16_t N;
  uint16_t hop;
  float gateMin;
  float gateMult;
  float yinThr;
  float confMin;
  uint32_t fs;
};

InstrumentProfile profiles[] = {
  { "Guitare",     80.f,  400.f, 1024,  512, 3.0f, 1.50f, 0.20f, 0.28f, 20000 },
  { "Violon",     190.f,  700.f, 1024,  512, 3.0f, 1.50f, 0.20f, 0.28f, 20000 },
  { "Ukulele",    260.f,  500.f, 1024,  512, 3.0f, 1.50f, 0.20f, 0.28f, 20000 },
  { "Basse",       30.f,  150.f, 2048, 1024, 3.0f, 1.50f, 0.20f, 0.28f, 20000 },
  { "Chromatique", 30.f, 2000.f,  768,  384, 4.0f, 1.70f, 0.20f, 0.40f, 20000 }
};

static const float CHROMA_MIN_EXCESS_RMS   = 4.0f;
static const float CHROMA_CONF_FLOOR       = 0.45f;
static const uint8_t CHROMA_CONFIRM_FRAMES = 4;
static const float CHROMA_FREQ_TOL_REL     = 0.025f;
static const float MAINS_REJECT_FMIN       = 47.0f;
static const float MAINS_REJECT_FMAX       = 53.0f;
static const float MAINS_REJECT_MAX_EXCESS = 8.0f;

volatile int instrumentIndex = 0;

// Cordes guitare standard
static const float guitarStringsHz[] = {
  82.4069f, 110.000f, 146.832f, 195.998f, 246.942f, 329.628f
};

// ============================================================
// FreeRTOS handles + sync
// ============================================================
TaskHandle_t taskAdcHandle = nullptr;
TaskHandle_t taskDspHandle = nullptr;
TaskHandle_t taskHapticHandle = nullptr;
TaskHandle_t taskLoggerHandle = nullptr;

portMUX_TYPE gRingMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE gStateMux = portMUX_INITIALIZER_UNLOCKED;

SemaphoreHandle_t i2cMutex = nullptr;

// ============================================================
// LOG QUEUE (événements DSP -> Logger)
// ============================================================
enum LogType : uint8_t {
  LOG_DETECT = 0,
  LOG_REJECT_SILENT,
  LOG_REJECT_NOPITCH,
  LOG_REJECT_LOWCONF,
  LOG_REJECT_IMPLAUSIBLE
};

struct LogEvent {
  LogType type;
  uint32_t tMs;

  char instr[16];
  char note[4];
  float f;
  int octave;
  float cents;
  int needle;
  float conf;
  float rms;
  float dRms;
  float gate;
  float noise;
  float f0;
  float lastAccepted;
};

QueueHandle_t logQueue = nullptr;
static const int LOG_QUEUE_LEN = 64;
volatile uint32_t logDropCount = 0;

// ============================================================
// RING BUFFER ADC -> DSP
// ============================================================
volatile uint16_t ringBuf[MAX_N];
volatile uint16_t ringWriteIndex = 0;
volatile uint16_t samplesSinceLastFrame = 0;
volatile uint16_t totalSamplesCaptured = 0;
volatile bool ringPrimed = false;
volatile bool frameReady = false;
volatile uint32_t frameOverrunCount = 0;

// ============================================================
// BUFFERS DSP
// ============================================================
float xCentered[MAX_N];
float xFiltered[MAX_N];
float yinDiff[MAX_N / 2 + 1];
float yinCmnd[MAX_N / 2 + 1];
uint16_t localSamples[MAX_N];
float hannWin[MAX_N];
bool hannInit = false;
uint16_t decimSamples[MAX_N];

// ============================================================
// SIGNAL / GATE / FILTER
// ============================================================
float noiseRmsEma = 0.0f;
bool noiseEmaInit = false;
static const float NOISE_EMA_ALPHA = 0.02f;

float hp_y_prev = 0.0f;
float hp_x_prev = 0.0f;
float lp_y_prev = 0.0f;

static const float HP_ALPHA = 0.9922f;
static const float LP_ALPHA = 0.470f;

// ============================================================
// LISSAGE / TRACKING
// ============================================================
float fPrev1 = 0.0f, fPrev2 = 0.0f;
bool havePrev1 = false, havePrev2 = false;
float fEma = 0.0f;
bool fEmaInit = false;
static const float FREQ_EMA_ALPHA = 0.35f;
float lastAcceptedFreq = 0.0f;

float candidateFreq = 0.0f;
uint8_t candidateCount = 0;
uint32_t lastValidDetectMs = 0;

// ============================================================
// STATS
// ============================================================
volatile uint32_t statFramesProcessed = 0;
volatile uint32_t statFramesSilent = 0;
volatile uint32_t statFramesDetected = 0;
volatile uint32_t statProcUsLast = 0;
volatile uint32_t statProcUsMax = 0;
volatile uint32_t statProcUsAvgAcc = 0;
volatile uint32_t statProcCount = 0;
volatile uint32_t statRejectNoPitch = 0;
volatile uint32_t statRejectLowConf = 0;
volatile uint32_t statRejectImplausible = 0;
uint32_t lastStatsPrintMs = 0;

// ============================================================
// NOTES / CENTS
// ============================================================
const char* noteNames[12] = {
  "C", "C#", "D", "D#", "E", "F",
  "F#", "G", "G#", "A", "A#", "B"
};

float frequencyToMidi(float f) { return 69.0f + 12.0f * log2f(f / 440.0f); }
float midiToFrequency(float midi) { return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f); }

void freqToNoteCents(float f, char* noteOut, size_t noteOutSize, int &octaveOut, float &centsOut)
{
  float midi = frequencyToMidi(f);
  int midiNearest = (int)roundf(midi);

  int noteIndex = ((midiNearest % 12) + 12) % 12;
  int octave = (midiNearest / 12) - 1;

  float fRef = midiToFrequency((float)midiNearest);
  float cents = 1200.0f * log2f(f / fRef);

  snprintf(noteOut, noteOutSize, "%s", noteNames[noteIndex]);
  octaveOut = octave;
  centsOut = cents;
}

int centsToNeedle(float cents)
{
  float x = cents / 50.0f;
  if (x > 1.0f) x = 1.0f;
  if (x < -1.0f) x = -1.0f;
  return (int)roundf(x * 100.0f);
}

// ============================================================
// LED HELPERS
// ============================================================
void setInstrumentLED(uint8_t r, uint8_t g, uint8_t b)
{
  ledcWriteChannel(CH_INST_R, r);
  ledcWriteChannel(CH_INST_G, g);
  ledcWriteChannel(CH_INST_B, b);
}

void setBatteryLED(uint8_t r, uint8_t g, uint8_t b)
{
  ledcWriteChannel(CH_BAT_R, r);
  ledcWriteChannel(CH_BAT_G, g);
  ledcWriteChannel(CH_BAT_B, b);
}

// ============================================================
// UTILS
// ============================================================
static inline bool decodeAdcSample(const uint8_t* p, uint16_t &outRaw12)
{
  const adc_digi_output_data_t* d = (const adc_digi_output_data_t*)p;
  if (d->type1.channel != ADC_CH) return false;
  outRaw12 = d->type1.data;
  return true;
}

static inline float median3(float a, float b, float c)
{
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

void resetTracking()
{
  havePrev1 = havePrev2 = false;
  fEmaInit = false;
  lastAcceptedFreq = 0.0f;
  candidateFreq = 0.0f;
  candidateCount = 0;
}

float smoothFrequencyRobust(float fRaw)
{
  float fMed = fRaw;
  if (!havePrev1) {
    fPrev1 = fRaw;
    havePrev1 = true;
  } else if (!havePrev2) {
    fPrev2 = fPrev1;
    fPrev1 = fRaw;
    havePrev2 = true;
  } else {
    fMed = median3(fRaw, fPrev1, fPrev2);
    fPrev2 = fPrev1;
    fPrev1 = fRaw;
  }

  if (!fEmaInit) {
    fEma = fMed;
    fEmaInit = true;
  } else {
    fEma = (1.0f - FREQ_EMA_ALPHA) * fEma + FREQ_EMA_ALPHA * fMed;
  }
  return fEma;
}

bool isFrequencyPlausible(float f)
{
  if (lastAcceptedFreq <= 0.0f) return true;

  float ratio = f / lastAcceptedFreq;
  if (ratio < 1.0f) ratio = 1.0f / ratio;

  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  if (idxLocal == 0) return ratio <= 2.2f; // guitare
  return ratio <= 1.6f;
}

void initHannWindow()
{
  for (uint16_t i = 0; i < MAX_N; i++) {
    hannWin[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float)i / (float)(MAX_N - 1));
  }
  hannInit = true;
}

bool isCloseFreq(float a, float b, float relTol = 0.04f)
{
  if (a <= 0.0f || b <= 0.0f) return false;
  float r = a / b;
  if (r < 1.0f) r = 1.0f / r;
  return r <= (1.0f + relTol);
}

float centsToRef(float f, float ref) { return 1200.0f * log2f(f / ref); }

float bestAbsCentsToGuitarStrings(float f)
{
  float best = 1e9f;
  for (float ref : guitarStringsHz) {
    float c = fabsf(centsToRef(f, ref));
    if (c < best) best = c;
  }
  return best;
}

float correctOctaveForGuitar(float f)
{
  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  if (idxLocal != 0) return f;

  InstrumentProfile p = profiles[idxLocal];
  float f1 = f;
  float f2 = f * 2.0f;

  bool f1ok = (f1 >= p.fmin && f1 <= p.fmax);
  bool f2ok = (f2 >= p.fmin && f2 <= p.fmax);

  float c1 = f1ok ? bestAbsCentsToGuitarStrings(f1) : 1e9f;
  float c2 = f2ok ? bestAbsCentsToGuitarStrings(f2) : 1e9f;

  if (c2 + 8.0f < c1) return f2;
  return f1;
}

// ============================================================
// BATTERY SERIAL TEST HELPERS
// ============================================================
#if ENABLE_BATTERY_TASK && BATTERY_TEST_SERIAL
void requestBatteryRefresh()
{
  taskENTER_CRITICAL(&gStateMux);
  batteryForceRefresh = true;
  taskEXIT_CRITICAL(&gStateMux);
}

void printBatteryTestHelp()
{
  DBG_PRINTLN("\n[BAT TEST] Commandes:");
  DBG_PRINTLN("  bat help        -> aide");
  DBG_PRINTLN("  bat sim         -> mode simulation");
  DBG_PRINTLN("  bat real        -> mode capteur reel");
  DBG_PRINTLN("  bat soc <0-100> -> force SOC simule");
  DBG_PRINTLN("  bat vbat <V>    -> force tension simulee");
  DBG_PRINTLN("  bat show        -> affiche etat courant");
}

void handleBatterySerialCommands()
{
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  String lower = line;
  lower.toLowerCase();

  if (!lower.startsWith("bat")) return;

  if (lower == "bat" || lower == "bat help") {
    printBatteryTestHelp();
    return;
  }

  if (lower == "bat real") {
    taskENTER_CRITICAL(&gStateMux);
    batterySimMode = false;
    taskEXIT_CRITICAL(&gStateMux);

    requestBatteryRefresh();
    DBG_PRINTLN("[BAT] Retour mode capteur reel (MAX17048)");
    return;
  }

  if (lower == "bat sim") {
    taskENTER_CRITICAL(&gStateMux);
    batterySimMode = true;
    taskEXIT_CRITICAL(&gStateMux);

    requestBatteryRefresh();
    DBG_PRINTLN("[BAT] Mode simulation active");
    return;
  }

  if (lower.startsWith("bat soc ")) {
    float val = lower.substring(8).toFloat();
    if (val < 0.0f) val = 0.0f;
    if (val > 100.0f) val = 100.0f;

    taskENTER_CRITICAL(&gStateMux);
    batterySimMode = true;
    batterySimSoc = val;
    taskEXIT_CRITICAL(&gStateMux);

    requestBatteryRefresh();

    DBG_PRINT("[BAT] SOC simule = ");
    DBG_PRINT(val, 1);
    DBG_PRINTLN(" %");
    return;
  }

  if (lower.startsWith("bat vbat ")) {
    float val = lower.substring(9).toFloat();
    if (val < 2.5f) val = 2.5f;
    if (val > 4.5f) val = 4.5f;

    taskENTER_CRITICAL(&gStateMux);
    batterySimMode = true;
    batterySimVoltage = val;
    taskEXIT_CRITICAL(&gStateMux);

    requestBatteryRefresh();

    DBG_PRINT("[BAT] VBAT simulee = ");
    DBG_PRINT(val, 3);
    DBG_PRINTLN(" V");
    return;
  }

  if (lower == "bat show") {
    bool valid;
    float vbat, soc;
    BatteryLevel lvl;
    bool simMode;

    taskENTER_CRITICAL(&gStateMux);
    valid = batteryValid;
    vbat  = batteryVoltage;
    soc   = batterySoc;
    lvl   = batteryLevel;
    simMode = batterySimMode;
    taskEXIT_CRITICAL(&gStateMux);

    DBG_PRINT("[BAT] Mode=");
    DBG_PRINT(simMode ? "SIM" : "REAL");
    DBG_PRINT(" | ");

    if (!valid) {
      DBG_PRINTLN("Etat invalide/erreur");
      return;
    }

    DBG_PRINT("VBAT=");
    DBG_PRINT(vbat, 3);
    DBG_PRINT(" V | SOC=");
    DBG_PRINT(soc, 1);
    DBG_PRINT(" % | Etat=");
    if (lvl == BATTERY_OK) DBG_PRINTLN("OK");
    else if (lvl == BATTERY_LOW) DBG_PRINTLN("LOW");
    else if (lvl == BATTERY_CRITICAL) DBG_PRINTLN("CRITICAL");
    else DBG_PRINTLN("ERROR");
    return;
  }

  DBG_PRINTLN("[BAT] Commande inconnue. Tape: bat help");
}
#endif

// ============================================================
// LOG QUEUE HELPERS
// ============================================================
void pushLogEvent(const LogEvent &ev)
{
  if (!logQueue) return;
  BaseType_t ok = xQueueSend(logQueue, &ev, 0);
  if (ok != pdTRUE) logDropCount++;
}

void pushRejectLog(LogType type, float rms, float gate, float noise, float conf = 0.0f, float f0 = 0.0f, float lastAcc = 0.0f)
{
  LogEvent ev = {};
  ev.type = type;
  ev.tMs = millis();
  ev.rms = rms;
  ev.gate = gate;
  ev.noise = noise;
  ev.conf = conf;
  ev.f0 = f0;
  ev.lastAccepted = lastAcc;
  pushLogEvent(ev);
}

void pushDetectLog(const char* instr, const char* note, float f, int octave, float cents, int needle,
                   float conf, float rms, float dRms, float gate, float noise)
{
  LogEvent ev = {};
  ev.type = LOG_DETECT;
  ev.tMs = millis();
  snprintf(ev.instr, sizeof(ev.instr), "%s", instr);
  snprintf(ev.note, sizeof(ev.note), "%s", note);
  ev.f = f;
  ev.octave = octave;
  ev.cents = cents;
  ev.needle = needle;
  ev.conf = conf;
  ev.rms = rms;
  ev.dRms = dRms;
  ev.gate = gate;
  ev.noise = noise;
  pushLogEvent(ev);
}

// ============================================================
// ADC DMA INIT
// ============================================================
bool initContinuousADC(uint32_t sampleRateHz)
{
  adc_continuous_handle_cfg_t adc_config = {};
  adc_config.max_store_buf_size = 8192;
  adc_config.conv_frame_size = ADC_READ_LEN;

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

bool reconfigureContinuousADC(uint32_t newFs)
{
  if (adc_handle != NULL) {
    adc_continuous_stop(adc_handle);
    adc_continuous_deinit(adc_handle);
    adc_handle = NULL;
  }

  taskENTER_CRITICAL(&gRingMux);
  ringWriteIndex = 0;
  samplesSinceLastFrame = 0;
  totalSamplesCaptured = 0;
  ringPrimed = false;
  frameReady = false;
  frameOverrunCount = 0;
  taskEXIT_CRITICAL(&gRingMux);

  resetTracking();
  candidateFreq = 0.0f;
  candidateCount = 0;
  lastValidDetectMs = 0;

  noiseEmaInit = false;
  noiseRmsEma = 0.0f;

  hp_y_prev = 0.0f;
  hp_x_prev = 0.0f;
  lp_y_prev = 0.0f;

  gSampleRateHz = newFs;
  gFs = (float)newFs;

  return initContinuousADC(newFs);
}

// ============================================================
// RING BUFFER PUSH / FETCH
// ============================================================
void pushSampleRing(uint16_t s)
{
  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  InstrumentProfile p = profiles[idxLocal];
  uint16_t activeN = p.N;
  uint16_t activeHop = p.hop;

  taskENTER_CRITICAL(&gRingMux);

  ringBuf[ringWriteIndex] = s;
  ringWriteIndex = (ringWriteIndex + 1) % activeN;

  if (!ringPrimed) {
    if (totalSamplesCaptured < activeN) totalSamplesCaptured++;
    if (totalSamplesCaptured >= activeN) {
      ringPrimed = true;
      samplesSinceLastFrame = 0;
    }
    taskEXIT_CRITICAL(&gRingMux);
    return;
  }

  samplesSinceLastFrame++;
  if (samplesSinceLastFrame >= activeHop) {
    if (frameReady) frameOverrunCount++;
    frameReady = true;
    samplesSinceLastFrame = 0;
    taskEXIT_CRITICAL(&gRingMux);

    if (taskDspHandle) xTaskNotifyGive(taskDspHandle);
    return;
  }

  taskEXIT_CRITICAL(&gRingMux);
}

bool fetchReadyFrame(uint16_t* dst, uint16_t &activeNOut)
{
  bool ready = false;
  uint16_t start = 0;

  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  InstrumentProfile p = profiles[idxLocal];
  uint16_t activeN = p.N;

  taskENTER_CRITICAL(&gRingMux);
  if (frameReady) {
    frameReady = false;
    ready = true;
    activeNOut = activeN;
    start = ringWriteIndex;
  }
  taskEXIT_CRITICAL(&gRingMux);

  if (!ready) return false;

  for (uint16_t i = 0; i < activeN; i++) {
    uint16_t idx = (start + i) % activeN;
    dst[i] = ringBuf[idx];
  }
  return true;
}

// ============================================================
// PREP SIGNAL + GATE
// ============================================================
bool prepareSignalAndGate(const uint16_t* samples, uint16_t N,
                          float gateMin, float gateMult,
                          float &rmsOut, float &gateUsed)
{
  if (!hannInit) initHannWindow();

  float mean = 0.0f;
  for (uint16_t i = 0; i < N; i++) mean += (float)samples[i];
  mean /= (float)N;

  float sumSq = 0.0f;
  for (uint16_t i = 0; i < N; i++) {
    float x = (float)samples[i] - mean;
    xCentered[i] = x;

    float hp = x - hp_x_prev + HP_ALPHA * hp_y_prev;
    hp_x_prev = x;
    hp_y_prev = hp;

    float lp = (1.0f - LP_ALPHA) * hp + LP_ALPHA * lp_y_prev;
    lp_y_prev = lp;

    float v = lp * hannWin[i];
    xFiltered[i] = v;
    sumSq += v * v;
  }

  float rms = sqrtf(sumSq / (float)N);
  rmsOut = rms;

  // Init bruit à une valeur basse (pas au premier RMS)
  if (!noiseEmaInit) {
    noiseRmsEma = gateMin;
    noiseEmaInit = true;
  } else {
    float silenceLimit = gateMin * 0.85f;
    if (rms < silenceLimit) {
      noiseRmsEma = (1.0f - NOISE_EMA_ALPHA) * noiseRmsEma + NOISE_EMA_ALPHA * rms;
    }
  }

  float adaptiveGate = noiseRmsEma * gateMult;
  if (adaptiveGate > 8.0f) adaptiveGate = 8.0f;
  if (adaptiveGate < gateMin) adaptiveGate = gateMin;

  gateUsed = adaptiveGate;
  return (rms >= adaptiveGate);
}

// ============================================================
// YIN
// ============================================================
bool detectPitchYIN(const float* x, uint16_t N, float Fs,
                    float fmin, float fmax, float yinThreshold,
                    float &f0, float &confidence)
{
  int tauMin = (int)(Fs / fmax);
  int tauMax = (int)(Fs / fmin);

  if (tauMin < 2) tauMin = 2;
  if (tauMax >= (int)(N / 2)) tauMax = (N / 2) - 1;
  if (tauMin >= tauMax) return false;

  yinDiff[0] = 0.0f;
  for (int tau = 1; tau <= tauMax; tau++) {
    float s = 0.0f;
    for (int j = 0; j < (int)N - tau; j++) {
      float d = x[j] - x[j + tau];
      s += d * d;
    }
    yinDiff[tau] = s;
  }

  yinCmnd[0] = 1.0f;
  float runningSum = 0.0f;
  for (int tau = 1; tau <= tauMax; tau++) {
    runningSum += yinDiff[tau];
    yinCmnd[tau] = (runningSum <= 1e-12f) ? 1.0f : (yinDiff[tau] * (float)tau) / runningSum;
  }

  int tauBest = -1;
  for (int tau = tauMin; tau <= tauMax; tau++) {
    if (yinCmnd[tau] < yinThreshold) {
      while ((tau + 1) <= tauMax && yinCmnd[tau + 1] < yinCmnd[tau]) tau++;
      tauBest = tau;
      break;
    }
  }

  if (tauBest < 0) {
    float bestVal = 1e9f;
    for (int tau = tauMin; tau <= tauMax; tau++) {
      if (yinCmnd[tau] < bestVal) {
        bestVal = yinCmnd[tau];
        tauBest = tau;
      }
    }
  }

  if (tauBest <= 0) return false;

  float tauRefined = (float)tauBest;
  if (tauBest > tauMin && tauBest < tauMax) {
    float y1 = yinCmnd[tauBest - 1];
    float y2 = yinCmnd[tauBest];
    float y3 = yinCmnd[tauBest + 1];
    float denom = (y1 - 2.0f * y2 + y3);
    if (fabsf(denom) > 1e-12f) {
      float delta = 0.5f * (y1 - y3) / denom;
      if (delta > 1.0f) delta = 1.0f;
      if (delta < -1.0f) delta = -1.0f;
      tauRefined = (float)tauBest + delta;
    }
  }

  if (tauRefined <= 0.0f) return false;

  f0 = Fs / tauRefined;
  confidence = 1.0f - yinCmnd[tauBest];
  if (confidence < 0.0f) confidence = 0.0f;
  if (confidence > 1.0f) confidence = 1.0f;

  return true;
}

// ============================================================
// STATS / PROFIL
// ============================================================
void resetStats()
{
  statFramesProcessed = 0;
  statFramesSilent = 0;
  statFramesDetected = 0;
  statProcUsLast = 0;
  statProcUsMax = 0;
  statProcUsAvgAcc = 0;
  statProcCount = 0;
  frameOverrunCount = 0;
  statRejectNoPitch = 0;
  statRejectLowConf = 0;
  statRejectImplausible = 0;
  logDropCount = 0;
}

void printCpuStats()
{
  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  InstrumentProfile p = profiles[idxLocal];
  float hopMs = 1000.0f * (float)p.hop / gFs;
  uint32_t budgetUs = (uint32_t)(1000000.0f * (float)p.hop / gFs);
  uint32_t avgUs = (statProcCount > 0) ? (statProcUsAvgAcc / statProcCount) : 0;

  DBG_PRINTLN("\n=== CPU / DSP Stats ===");
  DBG_PRINT("Profile: "); DBG_PRINTLN(p.name);
  DBG_PRINT("FS="); DBG_PRINT(gFs, 1);
  DBG_PRINT(" Hz | N="); DBG_PRINT(p.N);
  DBG_PRINT(" | Hop="); DBG_PRINT(p.hop);
  DBG_PRINT(" | hopMs="); DBG_PRINT(hopMs, 2);
  DBG_PRINT(" ms | budget="); DBG_PRINT(budgetUs);
  DBG_PRINTLN(" us");

  DBG_PRINT("Proc last="); DBG_PRINT(statProcUsLast);
  DBG_PRINT(" us | avg="); DBG_PRINT(avgUs);
  DBG_PRINT(" us | max="); DBG_PRINTLN(statProcUsMax);

  DBG_PRINT("Frames total="); DBG_PRINT(statFramesProcessed);
  DBG_PRINT(" | silent="); DBG_PRINT(statFramesSilent);
  DBG_PRINT(" | detected="); DBG_PRINTLN(statFramesDetected);

  DBG_PRINT("Overruns="); DBG_PRINTLN(frameOverrunCount);

  DBG_PRINT("Rejects: noPitch="); DBG_PRINT(statRejectNoPitch);
  DBG_PRINT(" | lowConf="); DBG_PRINT(statRejectLowConf);
  DBG_PRINT(" | implausible="); DBG_PRINTLN(statRejectImplausible);

  DBG_PRINT("Log drops="); DBG_PRINTLN(logDropCount);

  if (avgUs < (budgetUs / 2)) DBG_PRINTLN("CPU status: OK (large marge)");
  else if (avgUs < (budgetUs * 8 / 10)) DBG_PRINTLN("CPU status: OK (marge moyenne)");
  else DBG_PRINTLN("CPU status: LIMITE (optimiser)");
}

void updateInstrumentLed()
{
  int idx;
  taskENTER_CRITICAL(&gStateMux);
  idx = instrumentIndex;
  taskEXIT_CRITICAL(&gStateMux);

  if (idx < 0) idx = 0;
  if (idx >= (int)(sizeof(instrumentColors) / sizeof(instrumentColors[0]))) idx = 0;

  setInstrumentLED(
    instrumentColors[idx][0],
    instrumentColors[idx][1],
    instrumentColors[idx][2]
  );
}

void updateBatteryLedFromState()
{
#if ENABLE_BATTERY_TASK
  bool valid;
  BatteryLevel lvl;

  taskENTER_CRITICAL(&gStateMux);
  valid = batteryValid;
  lvl = batteryLevel;
  taskEXIT_CRITICAL(&gStateMux);

  if (!valid) {
    setBatteryLED(255, 0, 255); // violet = erreur
    return;
  }

  if (lvl == BATTERY_CRITICAL) {
    setBatteryLED(255, 0, 0);   // rouge
  } else if (lvl == BATTERY_LOW) {
    setBatteryLED(255, 80, 0);  // orange
  } else {
    setBatteryLED(0, 255, 0);   // vert
  }
#else
  setBatteryLED(0, 0, 0);
#endif
}

void onProfileChanged()
{
  int idxLocal;
  taskENTER_CRITICAL(&gStateMux);
  idxLocal = instrumentIndex;
  hapticNoteValid = false;
  hapticTuneAckSent = false;
  hapticMode = HAPTIC_IDLE;
  hapticNextTriggerMs = 0;
  hapticMuteUntilMs = millis() + 250;
  taskEXIT_CRITICAL(&gStateMux);

  InstrumentProfile p = profiles[idxLocal];

  if (gSampleRateHz != p.fs) {
    reconfigureContinuousADC(p.fs);
  } else {
    taskENTER_CRITICAL(&gRingMux);
    ringWriteIndex = 0;
    samplesSinceLastFrame = 0;
    totalSamplesCaptured = 0;
    ringPrimed = false;
    frameReady = false;
    frameOverrunCount = 0;
    taskEXIT_CRITICAL(&gRingMux);

    resetTracking();
    candidateFreq = 0.0f;
    candidateCount = 0;
    lastValidDetectMs = 0;

    noiseEmaInit = false;
    noiseRmsEma = 0.0f;

    hp_y_prev = 0.0f;
    hp_x_prev = 0.0f;
    lp_y_prev = 0.0f;
  }

  resetStats();
  updateInstrumentLed();
}

// ============================================================
// TASK ADC (Core 0)
// ============================================================
void taskAdc(void* param)
{
  (void)param;

  for (;;) {
    uint32_t bytesRead = 0;
    esp_err_t err = adc_continuous_read(adc_handle, adc_dma_buffer, ADC_READ_LEN, &bytesRead, 2);

    if (err == ESP_OK && bytesRead > 0) {
      const uint32_t sampleSize = SOC_ADC_DIGI_RESULT_BYTES;
      for (uint32_t i = 0; i + sampleSize <= bytesRead; i += sampleSize) {
        uint16_t raw12;
        if (decodeAdcSample(&adc_dma_buffer[i], raw12)) pushSampleRing(raw12);
      }
    }

    taskYIELD();
  }
}

// ============================================================
// TASK DSP (Core 1)
// ============================================================
inline void finalizeFrameStats(uint32_t t0)
{
  uint32_t dt = micros() - t0;
  statProcUsLast = dt;
  if (dt > statProcUsMax) statProcUsMax = dt;
  statProcUsAvgAcc += dt;
  statProcCount++;
  statFramesProcessed++;
}

void taskDsp(void* param)
{
  (void)param;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

    while (true) {
      uint16_t activeN = 0;
      if (!fetchReadyFrame(localSamples, activeN)) break;

      uint16_t procN = activeN;
      const uint16_t* procSamples = localSamples;
      float procFs = gFs;

      int idxLocal;
      uint32_t muteUntil;
      taskENTER_CRITICAL(&gStateMux);
      idxLocal = instrumentIndex;
      muteUntil = hapticMuteUntilMs;
      taskEXIT_CRITICAL(&gStateMux);

      if (idxLocal == 3) { // basse
        procN = activeN / 2;
        for (uint16_t i = 0; i < procN; i++) decimSamples[i] = localSamples[i * 2];
        procSamples = decimSamples;
        procFs = gFs * 0.5f;
      }

      uint32_t t0 = micros();
      uint32_t nowMs = millis();

      // Mute temporaire après vibration
      if (nowMs < muteUntil) {
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        resetTracking();
        finalizeFrameStats(t0);
        continue;
      }

      if (lastValidDetectMs != 0 && (nowMs - lastValidDetectMs) > 350) {
        resetTracking();
      }

      InstrumentProfile p = profiles[idxLocal];
      bool isChromatic = (idxLocal == 4);

      float rms = 0.0f, gateUsed = 0.0f;
      bool hasSignal = prepareSignalAndGate(procSamples, procN, p.gateMin, p.gateMult, rms, gateUsed);

      if (!hasSignal) {
        statFramesSilent++;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        resetTracking();
        pushRejectLog(LOG_REJECT_SILENT, rms, gateUsed, noiseRmsEma);
        finalizeFrameStats(t0);
        continue;
      }

      float f0 = 0.0f, conf = 0.0f;
      bool ok = detectPitchYIN(xFiltered, procN, procFs, p.fmin, p.fmax, p.yinThr, f0, conf);

      if (!ok) {
        statRejectNoPitch++;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        candidateCount = 0;
        candidateFreq = 0.0f;
        pushRejectLog(LOG_REJECT_NOPITCH, rms, gateUsed, noiseRmsEma);
        finalizeFrameStats(t0);
        continue;
      }

      float confMinUsed = p.confMin;
      if (isChromatic && confMinUsed < CHROMA_CONF_FLOOR) confMinUsed = CHROMA_CONF_FLOOR;

      if (conf < confMinUsed) {
        statRejectLowConf++;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        candidateCount = 0;
        candidateFreq = 0.0f;
        pushRejectLog(LOG_REJECT_LOWCONF, rms, gateUsed, noiseRmsEma, conf);
        finalizeFrameStats(t0);
        continue;
      }

      f0 = correctOctaveForGuitar(f0);

      const float rmsExcessPreview = rms - noiseRmsEma;
      if (isChromatic &&
          f0 >= MAINS_REJECT_FMIN && f0 <= MAINS_REJECT_FMAX &&
          rmsExcessPreview < MAINS_REJECT_MAX_EXCESS) {
        candidateCount = 0;
        candidateFreq = 0.0f;
        finalizeFrameStats(t0);
        continue;
      }

      if (!isFrequencyPlausible(f0)) {
        statRejectImplausible++;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        candidateCount = 0;
        candidateFreq = 0.0f;
        pushRejectLog(LOG_REJECT_IMPLAUSIBLE, rms, gateUsed, noiseRmsEma, conf, f0, lastAcceptedFreq);
        finalizeFrameStats(t0);
        continue;
      }

      const float rmsExcess = rms - noiseRmsEma;
      float minExcess = isChromatic ? CHROMA_MIN_EXCESS_RMS : 2.0f;
      if (rmsExcess < minExcess) {
        candidateCount = 0;
        candidateFreq = 0.0f;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        finalizeFrameStats(t0);
        continue;
      }

      if (candidateCount == 0) {
        candidateFreq = f0;
        candidateCount = 1;
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        finalizeFrameStats(t0);
        continue;
      }

      float relTol = isChromatic ? CHROMA_FREQ_TOL_REL : 0.04f;
      if (!isCloseFreq(f0, candidateFreq, relTol)) {
        candidateFreq = f0;
        candidateCount = 1;
        finalizeFrameStats(t0);
        continue;
      }

      candidateCount++;
      candidateFreq = 0.7f * candidateFreq + 0.3f * f0;

      uint8_t confirmNeeded = isChromatic ? CHROMA_CONFIRM_FRAMES : 3;
      if (candidateCount < confirmNeeded) {
        taskENTER_CRITICAL(&gStateMux);
        hapticNoteValid = false;
        taskEXIT_CRITICAL(&gStateMux);
        finalizeFrameStats(t0);
        continue;
      }

      // Candidat validé
      f0 = candidateFreq;
      candidateCount = 0;

      float fSmooth = smoothFrequencyRobust(f0);
      lastAcceptedFreq = fSmooth;

      char note[4];
      int octave = 0;
      float cents = 0.0f;
      freqToNoteCents(fSmooth, note, sizeof(note), octave, cents);
      int needle = centsToNeedle(cents);

      statFramesDetected++;

      taskENTER_CRITICAL(&gStateMux);
      hapticLastCents = cents;
      hapticLastNoteMs = nowMs;
      hapticNoteValid = true;
      taskEXIT_CRITICAL(&gStateMux);

      lastValidDetectMs = nowMs;

      pushDetectLog(p.name, note, fSmooth, octave, cents, needle, conf, rms, rmsExcess, gateUsed, noiseRmsEma);

      finalizeFrameStats(t0);
    }

    vTaskDelay(1);
  }
}

// ============================================================
// UI / BUTTON
// ============================================================
void IRAM_ATTR onButtonFallingISR()
{
  btnIrqFlag = true;
  btnIrqCount++;
}

void handleButtonInstrumentSelect()
{
  if (!btnIrqFlag) return;

  uint32_t now = millis();

  if ((now - lastBtnHandledMs) < BTN_DEBOUNCE_MS) {
    btnIrqFlag = false;
    return;
  }

  if (digitalRead(BTN_PIN) != LOW) {
    btnIrqFlag = false;
    return;
  }

  btnIrqFlag = false;
  lastBtnHandledMs = now;

  taskENTER_CRITICAL(&gStateMux);
  int nextIdx = instrumentIndex + 1;
  if (nextIdx >= (int)(sizeof(profiles) / sizeof(profiles[0]))) nextIdx = 0;
  instrumentIndex = nextIdx;
  taskEXIT_CRITICAL(&gStateMux);

  onProfileChanged();
}

// ============================================================
// DRV2605L I2C
// ============================================================
bool drvWrite8(uint8_t reg, uint8_t val)
{
  if (!i2cMutex) return false;
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(2)) != pdTRUE) return false;

  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  Wire.write(val);
  bool ok = (Wire.endTransmission() == 0);

  xSemaphoreGive(i2cMutex);
  return ok;
}

bool drvRead8(uint8_t reg, uint8_t &val)
{
  if (!i2cMutex) return false;
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(2)) != pdTRUE) return false;

  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    xSemaphoreGive(i2cMutex);
    return false;
  }

  if (Wire.requestFrom((int)DRV_ADDR, 1) != 1) {
    xSemaphoreGive(i2cMutex);
    return false;
  }

  val = Wire.read();
  xSemaphoreGive(i2cMutex);
  return true;
}

bool drvLoadSinglePulse()
{
  if (hapticSeqLoaded == 1) return true;

  bool ok = true;
  ok &= drvWrite8(0x04, FX_PULSE);
  ok &= drvWrite8(0x05, 0);
  ok &= drvWrite8(0x06, 0);
  ok &= drvWrite8(0x07, 0);
  ok &= drvWrite8(0x08, 0);
  ok &= drvWrite8(0x09, 0);
  ok &= drvWrite8(0x0A, 0);
  ok &= drvWrite8(0x0B, 0);

  if (ok) hapticSeqLoaded = 1;
  return ok;
}

bool drvLoadDoublePulse()
{
  if (hapticSeqLoaded == 2) return true;

  bool ok = true;
  ok &= drvWrite8(0x04, FX_PULSE);
  ok &= drvWrite8(0x05, FX_PULSE);
  ok &= drvWrite8(0x06, 0);
  ok &= drvWrite8(0x07, 0);
  ok &= drvWrite8(0x08, 0);
  ok &= drvWrite8(0x09, 0);
  ok &= drvWrite8(0x0A, 0);
  ok &= drvWrite8(0x0B, 0);

  if (ok) hapticSeqLoaded = 2;
  return ok;
}

bool drvGo()
{
  uint32_t now = millis();

  if (now - drvLastGoMs < 20) return false;

  bool ok = drvWrite8(0x0C, 0x01);
  if (ok) {
    drvLastGoMs = now;

    taskENTER_CRITICAL(&gStateMux);
    hapticMuteUntilMs = now + 250;
    taskEXIT_CRITICAL(&gStateMux);

    resetTracking();
    lastValidDetectMs = 0;
  }
  return ok;
}

bool setupDRV2605L()
{
  uint8_t v = 0;

  if (!drvRead8(0x00, v)) return false;

  if (!drvWrite8(0x01, 0x80)) return false; // soft reset
  delay(20);

  if (!drvWrite8(0x01, 0x00)) return false; // Internal Trigger
  delay(5);

  if (!drvWrite8(0x03, 0x01)) return false; // Library ERM

  uint8_t fb = 0;
  if (!drvRead8(0x1A, fb)) return false;
  fb &= ~(1 << 7); // ERM
  if (!drvWrite8(0x1A, fb)) return false;

  uint8_t c3 = 0;
  if (!drvRead8(0x1D, c3)) return false;
  c3 |= (1 << 5); // open-loop ERM
  if (!drvWrite8(0x1D, c3)) return false;

  hapticSeqLoaded = 0;
  if (!drvLoadSinglePulse()) return false;

  return true;
}

// ============================================================
// HAPTIC TASK
// ============================================================
void updateHapticFeedback()
{
  if (!hapticDrvOk) return;

  uint32_t now = millis();

  bool noteValid;
  float cents;
  uint32_t noteMs;

  taskENTER_CRITICAL(&gStateMux);
  noteValid = hapticNoteValid;
  cents     = hapticLastCents;
  noteMs    = hapticLastNoteMs;
  taskEXIT_CRITICAL(&gStateMux);

  bool validRecent = noteValid && ((now - noteMs) < 250);

  if (!validRecent) {
    hapticMode = HAPTIC_IDLE;
    hapticTuneAckSent = false;
    return;
  }

  float absC = fabsf(cents);

  HapticMode wantedMode;
  if (absC <= HAPTIC_TUNE_TOL_CENTS)      wantedMode = HAPTIC_IN_TUNE;
  else if (cents < 0.0f)                  wantedMode = HAPTIC_TOO_LOW;
  else                                    wantedMode = HAPTIC_TOO_HIGH;

  if (wantedMode != hapticMode) {
    hapticMode = wantedMode;
    hapticNextTriggerMs = 0;
    if (hapticMode != HAPTIC_IN_TUNE) hapticTuneAckSent = false;
  }

  if (hapticMode == HAPTIC_IN_TUNE) {
    if (!hapticTuneAckSent && now >= hapticNextTriggerMs) {
      if (drvLoadDoublePulse()) drvGo();
      hapticTuneAckSent = true;
      hapticNextTriggerMs = now + 500;
    }
    return;
  }

  if (now < hapticNextTriggerMs) return;

  if (drvLoadSinglePulse()) drvGo();

  if (hapticMode == HAPTIC_TOO_LOW) {
    if      (absC > 25.0f) hapticNextTriggerMs = now + 380;
    else if (absC > 10.0f) hapticNextTriggerMs = now + 500;
    else                   hapticNextTriggerMs = now + 650;
  } else {
    if      (absC > 25.0f) hapticNextTriggerMs = now + 180;
    else if (absC > 10.0f) hapticNextTriggerMs = now + 260;
    else                   hapticNextTriggerMs = now + 360;
  }
}

void taskHaptic(void* param)
{
  (void)param;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    updateHapticFeedback();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5));
  }
}

// ============================================================
// LOGGER TASK (logs strictement après DSP, via queue)
// ============================================================
void taskLogger(void* param)
{
  (void)param;

  LogEvent ev;
  for (;;) {
    if (xQueueReceive(logQueue, &ev, portMAX_DELAY) != pdTRUE) continue;

    switch (ev.type) {
      case LOG_DETECT:
        DBG_PRINT("Instrument=");
        DBG_PRINT(ev.instr);
        DBG_PRINT(" | f=");
        DBG_PRINT(ev.f, 2);
        DBG_PRINT(" Hz | note=");
        DBG_PRINT(ev.note);
        DBG_PRINT(ev.octave);
        DBG_PRINT(" | cents=");
        if (ev.cents >= 0) DBG_PRINT("+");
        DBG_PRINT(ev.cents, 1);
        DBG_PRINT(" | needle=");
        DBG_PRINT(ev.needle);
        DBG_PRINT(" | conf=");
        DBG_PRINT(ev.conf, 2);
        DBG_PRINT(" | RMS=");
        DBG_PRINT(ev.rms, 1);
        DBG_PRINT(" | dRMS=");
        DBG_PRINT(ev.dRms, 1);
        DBG_PRINT(" | Gate=");
        DBG_PRINT(ev.gate, 1);
        DBG_PRINT(" | Noise=");
        DBG_PRINTLN(ev.noise, 1);
        break;

      // Décommente si tu veux les rejets dans le terminal
      /*
      case LOG_REJECT_SILENT:
        DBG_PRINT("Silence/Bruit faible | RMS=");
        DBG_PRINT(ev.rms, 1);
        DBG_PRINT(" | Gate=");
        DBG_PRINT(ev.gate, 1);
        DBG_PRINT(" | Noise=");
        DBG_PRINTLN(ev.noise, 1);
        break;

      case LOG_REJECT_NOPITCH:
        DBG_PRINT("Rejet noPitch | RMS=");
        DBG_PRINT(ev.rms, 1);
        DBG_PRINT(" | Gate=");
        DBG_PRINT(ev.gate, 1);
        DBG_PRINT(" | Noise=");
        DBG_PRINTLN(ev.noise, 1);
        break;

      case LOG_REJECT_LOWCONF:
        DBG_PRINT("Rejet lowConf | conf=");
        DBG_PRINT(ev.conf, 2);
        DBG_PRINT(" | RMS=");
        DBG_PRINT(ev.rms, 1);
        DBG_PRINT(" | Gate=");
        DBG_PRINT(ev.gate, 1);
        DBG_PRINT(" | Noise=");
        DBG_PRINTLN(ev.noise, 1);
        break;

      case LOG_REJECT_IMPLAUSIBLE:
        DBG_PRINT("Rejet implausible | f0=");
        DBG_PRINT(ev.f0, 2);
        DBG_PRINT(" | last=");
        DBG_PRINT(ev.lastAccepted, 2);
        DBG_PRINT(" | conf=");
        DBG_PRINTLN(ev.conf, 2);
        break;
      */
      default:
        break;
    }
  }
}

// ============================================================
// BATTERY TASK
// ============================================================
#if ENABLE_BATTERY_TASK
void taskBattery(void* param)
{
  (void)param;

  uint32_t nextReadMs = millis(); // lecture immédiate au démarrage

  for (;;) {
    uint32_t now = millis();
    bool doRead = false;

    // Lecture périodique
    if ((int32_t)(now - nextReadMs) >= 0) {
      doRead = true;
    }

    // Force refresh (commande série)
    #if BATTERY_TEST_SERIAL
    bool forceNow = false;
    taskENTER_CRITICAL(&gStateMux);
    if (batteryForceRefresh) {
      batteryForceRefresh = false;
      forceNow = true;
    }
    taskEXIT_CRITICAL(&gStateMux);

    if (forceNow) doRead = true;
    #endif

    if (doRead) {
      float vbat = NAN;
      float soc  = NAN;
      bool readOk = false;

      #if BATTERY_TEST_SERIAL
      bool simMode;
      float simSoc, simVbat;

      taskENTER_CRITICAL(&gStateMux);
      simMode = batterySimMode;
      simSoc = batterySimSoc;
      simVbat = batterySimVoltage;
      taskEXIT_CRITICAL(&gStateMux);

      if (simMode) {
        vbat = simVbat;
        soc  = simSoc;
        readOk = true;
      } else
      #endif
      {
        if (i2cMutex && xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          vbat = maxlipo.cellVoltage();
          soc  = maxlipo.cellPercent();
          xSemaphoreGive(i2cMutex);
          readOk = true;
        }
      }

      BatteryLevel lvl = BATTERY_ERROR;
      bool valid = false;

      if (readOk && !isnan(vbat) && !isnan(soc)) {
        valid = true;
        if (soc < 0.0f) soc = 0.0f;
        if (soc > 100.0f) soc = 100.0f;

        if (soc <= BAT_RED_PCT)         lvl = BATTERY_CRITICAL;
        else if (soc <= BAT_ORANGE_PCT) lvl = BATTERY_LOW;
        else                            lvl = BATTERY_OK;
      }

      taskENTER_CRITICAL(&gStateMux);
      batteryValid = valid;
      batteryVoltage = valid ? vbat : 0.0f;
      batterySoc = valid ? soc : 0.0f;
      batteryLevel = lvl;
      taskEXIT_CRITICAL(&gStateMux);

      updateBatteryLedFromState();

      if (valid) {
        DBG_PRINT("[BAT] ");
        #if BATTERY_TEST_SERIAL
          DBG_PRINT(simMode ? "(SIM) " : "(REAL) ");
        #else
          DBG_PRINT("(REAL) ");
        #endif
        DBG_PRINT("VBAT=");
        DBG_PRINT(vbat, 3);
        DBG_PRINT(" V | SOC=");
        DBG_PRINT(soc, 1);
        DBG_PRINT(" % | Etat=");
        if (lvl == BATTERY_OK) DBG_PRINTLN("OK");
        else if (lvl == BATTERY_LOW) DBG_PRINTLN("LOW");
        else if (lvl == BATTERY_CRITICAL) DBG_PRINTLN("CRITICAL");
        else DBG_PRINTLN("ERROR");
      } else {
        DBG_PRINTLN("[BAT] Erreur lecture");
      }

      // prochaine lecture périodique à +60s (même après force refresh)
      nextReadMs = millis() + BATTERY_PERIOD_MS;
    }

    vTaskDelay(pdMS_TO_TICKS(BATTERY_TASK_TICK_MS));
  }
}
#endif

// ============================================================
// SETUP / LOOP
// ============================================================
void setup()
{
  Serial.begin(115200);

  i2cMutex = xSemaphoreCreateMutex();
  logQueue = xQueueCreate(LOG_QUEUE_LEN, sizeof(LogEvent));

  if (!i2cMutex) DBG_PRINTLN("Erreur creation mutex I2C");
  if (!logQueue) DBG_PRINTLN("Erreur creation queue logs");

  // I2C partagé (DRV2605L + MAX17048)
  Wire.begin(HAPTIC_SDA_PIN, HAPTIC_SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(2);

  // Init jauge batterie MAX17048 (même bus I2C)
  #if ENABLE_BATTERY_TASK
  bool batteryGaugeOk = false;
  if (i2cMutex && xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    batteryGaugeOk = maxlipo.begin();
    xSemaphoreGive(i2cMutex);
  }

  if (batteryGaugeOk) {
    // getChipID fait aussi de l'I2C, on protège pareil
    uint16_t chip = 0;
    if (i2cMutex && xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      chip = maxlipo.getChipID();
      xSemaphoreGive(i2cMutex);
    }
    DBG_PRINT("[BAT] MAX17048 OK. Chip ID: 0x");
    DBG_PRINTLN(chip, HEX);
  } else {
    DBG_PRINTLN("[BAT] MAX17048 introuvable (mode REAL indisponible, mode SIM possible)");
  }
  #endif

  // Haptique
  hapticDrvOk = setupDRV2605L();
  if (hapticDrvOk) {
    drvLoadSinglePulse();
    drvGo();
    delay(500);
  }

  // LED instrument
  ledcAttachChannel(LED_INST_R, PWM_FREQ, PWM_RES, CH_INST_R);
  ledcAttachChannel(LED_INST_G, PWM_FREQ, PWM_RES, CH_INST_G);
  ledcAttachChannel(LED_INST_B, PWM_FREQ, PWM_RES, CH_INST_B);
  setInstrumentLED(0, 0, 0);

  // LED batterie
  ledcAttachChannel(LED_BAT_R, PWM_FREQ, PWM_RES, CH_BAT_R);
  ledcAttachChannel(LED_BAT_G, PWM_FREQ, PWM_RES, CH_BAT_G);
  ledcAttachChannel(LED_BAT_B, PWM_FREQ, PWM_RES, CH_BAT_B);
  setBatteryLED(0, 0, 0);

  // Bouton
  pinMode(BTN_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButtonFallingISR, FALLING);
  delay(400);

  DBG_PRINTLN("\n=== ESP32 WROVER-E Accordeur V3 (FreeRTOS + Queue Logs + Battery Task) ===");
  DBG_PRINTF("ADC_PIN=%d (ADC1_CH7) | FS=%.1f Hz\n", ADC_PIN, gFs);
  DBG_PRINTLN("Taches: ADC(core0) + DSP(core1) + HAPTIC(core0) + LOGGER(core1) + BATTERY(core0)");

  #if ENABLE_BATTERY_TASK && BATTERY_TEST_SERIAL
    printBatteryTestHelp();
  #endif

  gSampleRateHz = profiles[instrumentIndex].fs;
  gFs = (float)gSampleRateHz;
  if (!initContinuousADC(gSampleRateHz)) {
    DBG_PRINTLN("Erreur init ADC continu.");
    while (true) delay(1000);
  }

  initHannWindow();
  onProfileChanged();
  updateInstrumentLed();
  updateBatteryLedFromState();

  DBG_PRINTLN("ADC continu demarre.");

  xTaskCreatePinnedToCore(taskAdc,    "ADC_Task",     4096,  nullptr, 2, &taskAdcHandle,    0);
  xTaskCreatePinnedToCore(taskDsp,    "DSP_Task",    12288,  nullptr, 3, &taskDspHandle,    1);
  xTaskCreatePinnedToCore(taskHaptic, "Haptic_Task",  4096,  nullptr, 1, &taskHapticHandle, 0);
  xTaskCreatePinnedToCore(taskLogger, "Logger_Task",  4096,  nullptr, 1, &taskLoggerHandle, 1);

  #if ENABLE_BATTERY_TASK
    xTaskCreatePinnedToCore(taskBattery, "Battery_Task", 4096, nullptr, 1, &taskBatteryHandle, 0);
  #endif
}

void loop()
{
  handleButtonInstrumentSelect();

  #if ENABLE_BATTERY_TASK && BATTERY_TEST_SERIAL
    handleBatterySerialCommands();
  #endif

  uint32_t now = millis();
  if (now - lastStatsPrintMs > 5000) {
    printCpuStats();
    lastStatsPrintMs = now;
  }

  delay(10);
}
