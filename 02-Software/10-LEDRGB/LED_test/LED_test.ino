// Configuration des broches ESP32 (selon ton schéma)
//static const int LED_R = 27;
//static const int LED_G = 12;
//static const int LED_B = 14;

static const int LED_R = 32;
static const int LED_G = 25;
static const int LED_B = 33;

// Paramètres PWM ESP32
static const int PWM_FREQ = 5000;
static const int PWM_RES  = 8;   // Résolution 8 bits (0-255)
static const int CH_R = 0;
static const int CH_G = 1;
static const int CH_B = 2;

// Tableau des 10 couleurs (Rouge, Vert, Bleu)
uint8_t colorPalette[10][3] = {
  {255, 0, 0},      // 1. Rouge pur
  {0, 255, 0},      // 2. Vert pur
  {0, 0, 255},      // 3. Bleu pur
  {255, 0, 255},    // 4. VIOLET (Magenta)
  {255, 80, 0},     // 5. Orange (comme ton code batterie)
  {255, 255, 0},    // 6. Jaune
  {0, 255, 255},    // 7. Cyan (Bleu ciel)
  {128, 0, 255},    // 8. Pourpre (Violet foncé)
  {255, 20, 147},   // 9. Rose profond
  {255, 255, 255}   // 10. Blanc froid
};

// Fonction pour appliquer la couleur (Cathode Commune)
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

void setup() {
  Serial.begin(115200);
  
  // Initialisation PWM
  ledcSetup(CH_R, PWM_FREQ, PWM_RES);
  ledcSetup(CH_G, PWM_FREQ, PWM_RES);
  ledcSetup(CH_B, PWM_FREQ, PWM_RES);

  ledcAttachPin(LED_R, CH_R);
  ledcAttachPin(LED_G, CH_G);
  ledcAttachPin(LED_B, CH_B);

  Serial.println("Test LED RGB démarré...");
}

void loop() {
  for (int i = 0; i < 10; i++) {
    Serial.print("Couleur n°");
    Serial.println(i + 1);
    
    // Application de la couleur depuis le tableau
    setRGB(colorPalette[i][0], colorPalette[i][1], colorPalette[i][2]);
    
    delay(1000); // Pause de 1 seconde
  }
}
