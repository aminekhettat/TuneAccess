#include <Wire.h>
#include "Adafruit_MAX1704X.h"

Adafruit_MAX17048 maxlipo;

// >>>> À ADAPTER SI TON RASPIAUDIO UTILISE D'AUTRES PINS I2C <<<<
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("MAX17048 - Lecture pourcentage batterie (I2C)");

  // Init I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Init capteur
  while (!maxlipo.begin()) {
    Serial.println("Impossible de trouver le MAX17048. Verifie le cablage et qu'une batterie est branchee !");
    delay(2000);
  }

  Serial.print("MAX17048 detecte. Chip ID: 0x");
  Serial.println(maxlipo.getChipID(), HEX);

  // Optionnel : quickStart (reset estimation SOC). A utiliser avec prudence.
  // maxlipo.quickStart();
}

void loop() {
  float vbat = maxlipo.cellVoltage();
  float soc  = maxlipo.cellPercent();

  if (isnan(vbat) || isnan(soc)) {
    Serial.println("Erreur lecture (NaN). Batterie branchee ? Niveau trop bas ?");
    delay(2000);
    return;
  }

  Serial.print("VBAT = ");
  Serial.print(vbat, 3);
  Serial.print(" V | Batterie = ");
  Serial.print(soc, 1);
  Serial.println(" %");

  delay(1000);  // evite de lire trop souvent
}
