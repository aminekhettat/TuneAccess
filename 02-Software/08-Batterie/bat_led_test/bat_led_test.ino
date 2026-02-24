#include <Wire.h>
#include "Adafruit_MAX1704X.h"
#include <math.h>

Adafruit_MAX17048 maxlipo;

// I2C
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;

// LED RGB L-154A4SURKQBDZGC = cathode commune (patte commune au GND)
// 26 occupé -> on utilise 33 pour le vert
static const int LED_R = 27;
static const int LED_G = 12;
static const int LED_B = 14;

// PWM (ESP32)
static const int PWM_FREQ = 5000;
static const int PWM_RES  = 8;   // 0..255
static const int CH_R = 0;
static const int CH_G = 1;
static const int CH_B = 2;

// Seuils batterie (%)
static const float ORANGE_PCT = 30.0; // <= 30% : orange
static const float RED_PCT    = 10.0; // <= 10% : rouge

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

void setupPWM() {
  ledcSetup(CH_R, PWM_FREQ, PWM_RES);
  ledcSetup(CH_G, PWM_FREQ, PWM_RES);
  ledcSetup(CH_B, PWM_FREQ, PWM_RES);

  ledcAttachPin(LED_R, CH_R);
  ledcAttachPin(LED_G, CH_G);
  ledcAttachPin(LED_B, CH_B);

  setRGB(0, 0, 0); // OFF
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nMAX17048 + LED RGB (Vert/Orange/Rouge)");

  Wire.begin(I2C_SDA, I2C_SCL);
  setupPWM();

  while (!maxlipo.begin()) {
    Serial.println("MAX17048 introuvable. Verifie cablage + batterie !");
    setRGB(255, 0, 255); // violet = erreur capteur
    delay(2000);
  }

  Serial.print("MAX17048 OK. Chip ID: 0x");
  Serial.println(maxlipo.getChipID(), HEX);
}

void loop() {
  float vbat = maxlipo.cellVoltage();
  float soc  = maxlipo.cellPercent();

  if (isnan(vbat) || isnan(soc)) {
    Serial.println("Erreur lecture (NaN).");
    setRGB(255, 0, 255);
    delay(2000);
    return;
  }

  Serial.print("VBAT=");
  Serial.print(vbat, 3);
  Serial.print(" V | SOC=");
  Serial.print(soc, 1);
  Serial.println(" %");

  // <=10% : Rouge
  if (soc <= RED_PCT) {
    setRGB(255, 0, 0);
  }
  // <=30% : Orange (faible)
  else if (soc <= ORANGE_PCT) {
    setRGB(255, 80, 0); // orange
  }
  // >30% : Vert
  else {
    setRGB(0, 255, 0);
  }

  delay(1000);
}
