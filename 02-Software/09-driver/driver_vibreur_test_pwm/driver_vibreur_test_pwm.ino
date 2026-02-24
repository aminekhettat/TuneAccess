#include <Wire.h>

#define SDA_PIN   18 //21
#define SCL_PIN   19 //22
#define DRV_ADDR  0x5A   // essaie 0x5B si besoin

void w8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.print("I2C write error reg 0x");
    Serial.print(reg, HEX);
    Serial.print(" -> ");
    Serial.println(err);
  }
}

uint8_t r8(uint8_t reg) {
  Wire.beginTransmission(DRV_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(DRV_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void setupDRV_InternalTrigger() {
  // Soft reset
  w8(0x01, 0x80);
  delay(20);

  // Mode = Internal Trigger (0x00)
  w8(0x01, 0x00);
  delay(5);

  // Library = 1 (ERM)
  w8(0x03, 0x01);

  // ERM (bit7=0 dans 0x1A)
  uint8_t fb = r8(0x1A);
  fb &= ~(1 << 7);
  w8(0x1A, fb);

  // Open-loop ERM (bit5=1 dans 0x1D)
  uint8_t c3 = r8(0x1D);
  c3 |= (1 << 5);
  w8(0x1D, c3);

  // Séquence d’effet : Reg 0x04 = effet #1, Reg 0x05 = 0 (fin)
  // Essaie plusieurs IDs si besoin (1, 47, 52, 58...)
  w8(0x04, 47);   // effet fort (souvent)
  w8(0x05, 0);    // fin séquence

  Serial.print("STATUS = 0x"); Serial.println(r8(0x00), HEX);
  Serial.print("MODE   = 0x"); Serial.println(r8(0x01), HEX);
  Serial.print("LIB    = 0x"); Serial.println(r8(0x03), HEX);
  Serial.print("FB     = 0x"); Serial.println(r8(0x1A), HEX);
  Serial.print("C3     = 0x"); Serial.println(r8(0x1D), HEX);
}

void playEffect() {
  // GO bit (reg 0x0C = 1)
  w8(0x0C, 0x01);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("Test DRV2605L effet interne...");
  setupDRV_InternalTrigger();
}

void loop() {
  Serial.println("Play effect");
  playEffect();
  delay(120);
}
