//#define BTN_PIN 34
//
//void setup() {
//  Serial.begin(115200);
//  pinMode(BTN_PIN, INPUT);   
//  delay(1000);
//  Serial.println("TEST BOUTON GPIO34");
//}
//
//void loop() {
//  int state = digitalRead(BTN_PIN);
//
//  if (state == LOW) {
//    Serial.println("APPUYE");
//  } else {
//    Serial.println("RELACHE");
//  }
//
//  delay(200);
//}


#define BTN_PIN 34

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT);   
  delay(1000);
  Serial.println("TEST BOUTON GPIO34");
}

void loop() {
  int state = digitalRead(BTN_PIN);

  if (state == LOW) {
    delay(200); 
    state = digitalRead(BTN_PIN);
    if (state == LOW){
      Serial.println("APPUYE");
    }
  } else {
    Serial.println("RELACHE");
  }

  delay(100);
}
