#include <Arduino.h>
#include "driver/i2s.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ============================================================================
// Configuration Matérielle
// ============================================================================
static const i2s_port_t I2S_PORT = I2S_NUM_0;
static const int I2S_BCK_PIN  = 5;
static const int I2S_WS_PIN   = 25;
static const int I2S_DATA_PIN = 35;
static const int NEOPIXEL_PIN = 4;
static const int NEOPIXEL_COUNT = 8;
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ============================================================================
// Profils d'instruments
// ============================================================================
enum Instrument { VIOLON, GUITARE, UKULELE, BASSE, CHROMATIQUE };
Instrument profilActuel = GUITARE;

struct Config {
    float fMin;
    float fMax;
    uint32_t couleur;
    const char* nom;
};

Config reglages[] = {
    {190.0, 700.0,  0xFF0000, "Violon"},
    {80.0,  400.0,  0x00FF00, "Guitare"},
    {260.0, 500.0,  0x0000FF, "Ukulele"},
    {30.0,  150.0,  0x800080, "Basse"},
    {30.0,  2000.0, 0xFFA500, "Chromatique"}
};

// ============================================================================
// Variables pour filtrage des sous-harmoniques
// ============================================================================
static float lastFreq = 0.0f;

// ============================================================================
// Variables Globales Audio
// ============================================================================
static const float SAMPLE_RATE = 20000.0f;
static const int   YIN_BUFFER_SIZE = 2048;
static float g_audioBuffer[YIN_BUFFER_SIZE];
static float g_yinBuffer[YIN_BUFFER_SIZE];

static float g_hpAlpha, g_lpAlpha;
static float g_hpY = 0, g_hpXprev = 0, g_lpY = 0;

static const int FREQ_HISTORY_SIZE = 3;
static float g_freqHistory[FREQ_HISTORY_SIZE];
static int   g_freqIndex = 0;
static bool  g_historyFilled = false;
static const float SILENCE_RMS_THRES = 0.001f;

// ============================================================================
// Utils
// ============================================================================
float computeRMS(const float *buffer, size_t length) {
    double sumSq = 0;
    for (size_t i = 0; i < length; i++) sumSq += (double)buffer[i] * buffer[i];
    return sqrt(sumSq / length);
}

// ============================================================================
// Traitement Audio & Filtres
// ============================================================================
void changerProfil(Instrument p) {
    profilActuel = p;
    Config c = reglages[p];
    float fHP = c.fMin * 0.8f;
    float fLP = c.fMax * 1.2f;
    g_hpAlpha = expf(-2.0f * 3.14159f * fHP / SAMPLE_RATE);
    g_lpAlpha = expf(-2.0f * 3.14159f * fLP / SAMPLE_RATE);
    g_hpY = 0; g_hpXprev = 0; g_lpY = 0;
    for(int i=0;i<FREQ_HISTORY_SIZE;i++) g_freqHistory[i]=0;
    g_historyFilled=false;
    Serial.printf("\n>>> Profil: %s (%.0fHz - %.0fHz)\n", c.nom, c.fMin, c.fMax);
}

void appliquerFiltre(float *buffer, int taille) {
    for (int i = 0; i < taille; i++) {
        float x = buffer[i];
        float yhp = g_hpAlpha * (g_hpY + x - g_hpXprev);
        g_hpY = yhp; g_hpXprev = x;
        float ylp = g_lpAlpha * g_lpY + (1.0f - g_lpAlpha) * yhp;
        g_lpY = ylp;
        buffer[i] = ylp;
    }
}

// ============================================================================
// YIN avec interpolation sub-échantillon (PRÉCISION 1 CENT)
// ============================================================================
float detectPitchYIN(const float *input) {
    int tauMin = (int)(SAMPLE_RATE / reglages[profilActuel].fMax);
    int tauMax = (int)(SAMPLE_RATE / reglages[profilActuel].fMin);
    if (tauMax >= YIN_BUFFER_SIZE-2) tauMax = YIN_BUFFER_SIZE-2;

    for (int tau = 0; tau <= tauMax; tau++) {
        double sum = 0;
        for (int i = 0; i < YIN_BUFFER_SIZE - tau; i++) {
            float d = input[i] - input[i + tau];
            sum += d * d;
        }
        g_yinBuffer[tau] = sum;
    }

    g_yinBuffer[0] = 1;
    double runningSum = 0;
    for (int tau = 1; tau <= tauMax; tau++) {
        runningSum += g_yinBuffer[tau];
        g_yinBuffer[tau] = g_yinBuffer[tau] * tau / runningSum;
    }

    int tauFound = -1;
    for (int tau = tauMin; tau <= tauMax; tau++) {
        if (g_yinBuffer[tau] < 0.15f) {
            while (tau+1 <= tauMax && g_yinBuffer[tau+1] < g_yinBuffer[tau]) tau++;
            tauFound = tau;
            break;
        }
    }

    if (tauFound < 1) return -1;

    float s0 = g_yinBuffer[tauFound-1];
    float s1 = g_yinBuffer[tauFound];
    float s2 = g_yinBuffer[tauFound+1];

    float denom = (2*s1 - s2 - s0);
    float delta = 0.0f;
    if (fabs(denom) > 1e-9f)
        delta = 0.5f * (s2 - s0) / denom;

    float tauInterp = tauFound + delta;

    for (int k = 2; k <= 4; k++) {
        int t = (int)(tauInterp / k);
        if (t >= tauMin && g_yinBuffer[t] < 0.20f)
            tauInterp /= k;
    }

    return SAMPLE_RATE / tauInterp;
}

// ============================================================================
// Post-traitement
// ============================================================================
float smoothFrequency(float f) {
    g_freqHistory[g_freqIndex++] = f;
    if (g_freqIndex >= FREQ_HISTORY_SIZE) {
        g_freqIndex = 0;
        g_historyFilled = true;
    }
    int n = g_historyFilled ? FREQ_HISTORY_SIZE : g_freqIndex;
    float sum = 0;
    for (int i = 0; i < n; i++) sum += g_freqHistory[i];
    return sum / n;
}

void frequencyToNote(float freq, const char **note, int *oct, float *cents, float *nearest) {
    static const char *names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    float n = 12.0f * log2f(freq / 440.0f);
    int midi = roundf(n) + 69;
    *note = names[midi % 12];
    *oct = midi / 12 - 1;
    *nearest = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
    *cents = 1200.0f * log2f(freq / *nearest);
}

// ============================================================================
// I2S
// ============================================================================
bool initI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_I2S;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;

    if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_DATA_PIN
    };
    return i2s_set_pin(I2S_PORT, &pins) == ESP_OK;
}

size_t readI2SBlock(float *buffer, size_t n) {
    static int32_t raw[256];
    size_t read = 0;
    while (read < n) {
        size_t bytes = 0;
        i2s_read(I2S_PORT, raw, sizeof(raw), &bytes, portMAX_DELAY);
        for (size_t i = 0; i < bytes/4 && read < n; i++)
            buffer[read++] = (raw[i] >> 8) / 8388608.0f;
    }
    return read;
}

// ============================================================================
// Setup & Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    pixels.begin();
    pixels.setBrightness(40);
    initI2S();
    changerProfil(CHROMATIQUE);
}




void loop() {
    readI2SBlock(g_audioBuffer, YIN_BUFFER_SIZE);
    appliquerFiltre(g_audioBuffer, YIN_BUFFER_SIZE);

    float f_micro = detectPitchYIN(g_audioBuffer);
    if (f_micro <= 0) return;

    float f_lisse = smoothFrequency(f_micro);

    // -------------------------------
    // Élimination sous-harmoniques
    // -------------------------------
    if (lastFreq > 0) {
        float ratio = f_lisse / lastFreq;

        if (ratio < 0.55f) {          // proche de 1/2
            f_lisse *= 2.0f;
        } else if (ratio < 0.7f) {    // proche de 2/3
            f_lisse *= 3.0f/2.0f;
        } else if (ratio > 1.4f) {    // proche de 2x
            f_lisse /= 2.0f;
        }
        // tu peux ajouter d'autres ratios si nécessaire
    }

    lastFreq = f_lisse;

    const char *note;
    int oct;
    float cents, ref;
    frequencyToNote(f_lisse, &note, &oct, &cents, &ref);

    // calcul de l'écart avec la fréquence micro
    float diffHz = f_lisse - f_micro;
    float diffCent = 1200.0f * log2f(f_lisse / f_micro);

    Serial.printf("[%s] f_micro: %.3f Hz | f_lissée: %.3f Hz | Diff: %+0.3f Hz | %+0.2f cents | Note: %s%d | Ecart: %+0.2f cents\n",
        reglages[profilActuel].nom, f_micro, f_lisse, diffHz, diffCent, note, oct, cents);

    uint32_t col = (fabs(cents) < 1.0f) ? 0x00FF00 : (fabs(cents) < 5 ? 0xFFFF00 : 0xFF0000);
    for(int i=0;i<NEOPIXEL_COUNT;i++) pixels.setPixelColor(i,col);
    pixels.show();
}
