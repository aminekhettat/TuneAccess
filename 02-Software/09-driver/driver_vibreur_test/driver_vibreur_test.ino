#include <Wire.h>

#define SDA_PIN   21
#define SCL_PIN   22
#define DRV_ADDR  0x5A

// Ecrit 1 octet dans un registre DRV2605L
void drvWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // Reset logiciel
  drvWrite(0x01, 0x80);
  delay(10);

  // MODE = 5 (RTP), STANDBY = 0
  drvWrite(0x01, 0x05);
  delay(5);
}

void loop() {
  // Vibre fort 1 seconde
  drvWrite(0x02, 200);   // RTP input (0..255)
  delay(10000);

  // Stop 1 seconde
  drvWrite(0x02, 0);
  delay(1000);
}
