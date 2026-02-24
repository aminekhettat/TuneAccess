#include <Arduino.h>

#define N_NOTES              12
#define N_OCT                9

//Table de notes 
struct Row_Note {
  const char* name;       // nom de la note
  float hz[N_OCT];            
};

static const Row_Note Note_Tab[N_NOTES] = {
  {"C",     {16.35, 32.70, 65.41, 130.81, 261.63, 523.25, 1046.50, 2093.00, 4186.01}},
  {"C#/Db", {17.32, 34.65, 69.30, 138.59, 277.18, 554.37, 1108.73, 2217.46, 4434.92}},
  {"D",     {18.35, 36.71, 73.42, 146.83, 293.66, 587.33, 1174.66, 2349.32, 4698.63}},
  {"D#/Eb", {19.45, 38.89, 77.78, 155.56, 311.13, 622.25, 1244.51, 2489.02, 4978.03}},
  {"E",     {20.60, 41.20, 82.41, 164.81, 329.63, 659.26, 1318.51, 2637.02, 5274.04}},
  {"F",     {21.83, 43.65, 87.31, 174.61, 349.23, 698.46, 1396.91, 2793.83, 5587.65}},
  {"F#/Gb", {23.12, 46.25, 92.50, 185.00, 369.99, 739.99, 1479.98, 2959.96, 5919.91}},
  {"G",     {24.50, 49.00, 98.00, 196.00, 392.00, 783.99, 1567.98, 3135.96, 6271.93}},
  {"G#/Ab", {25.96, 51.91, 103.83, 207.65, 415.30, 830.61, 1661.22, 3322.44, 6644.88}},
  {"A",     {27.50, 55.00, 110.00, 220.00, 440.00, 880.00, 1760.00, 3520.00, 7040.00}},
  {"A#/Bb", {29.14, 58.27, 116.54, 233.08, 466.16, 932.33, 1864.66, 3729.31, 7458.62}},
  {"B",     {30.87, 61.74, 123.47, 246.94, 493.88, 987.77, 1975.53, 3951.07, 7902.13}}
};

struct Gap_Info {
  const char* from_Name; // nom de la 1re note
  const char* to_Name; // nom de la 2e note
  float min_Hz; // plus petit distance en Hz entre les 2 notes
  int from_Oct;   // octave de la 1re note
  int to_Oct;     // octave de la 2e note
};

Gap_Info Gap_Info_Tab[N_NOTES];

void Min_Step() {
  for (int n=0; n<N_NOTES; ++n) {
    Gap_Info_Tab[n] = { Note_Tab[n].name,
                        Note_Tab[(n+1)%N_NOTES].name, // au cas où n = 11, n+1 = 0
                        1e4, // on initialise àune valeur max
                        -1, 
                        -1 };
  }

  // on parcourt toutes les octaves présentes (0..8 dans ta table)
  for (int o=0; o<N_OCT; ++o) { // on parcours des octaves
    // de 0 à 10 dans la même octave
    for (int n=0; n<N_NOTES - 1; ++n) { // on parcours les notes
      float f1 = Note_Tab[n].hz[o];
      float f2 = Note_Tab[n+1].hz[o];
      float d  = f2 - f1;
      if (d > 0.0 && d < Gap_Info_Tab[n].min_Hz) {
        Gap_Info_Tab[n].min_Hz  = d;
        Gap_Info_Tab[n].from_Oct = o;
        Gap_Info_Tab[n].to_Oct   = o;
      }
    }
    // pour 11 : B(o) -> C(o+1) si l'octave suivant existe
    if (o + 1 < N_OCT) {
      float f1 = Note_Tab[N_NOTES - 1].hz[o];
      float f2 = Note_Tab[0].hz[o+1];
      float d  = f2 - f1;
      if (d > 0.0 && d < Gap_Info_Tab[N_NOTES - 1].min_Hz) { // N_NOTES - 1 = 11
        Gap_Info_Tab[N_NOTES - 1].min_Hz  = d;
        Gap_Info_Tab[N_NOTES - 1].from_Oct = o;
        Gap_Info_Tab[N_NOTES - 1].to_Oct   = o+1;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Min_Step();

  Serial.println(F("La distance mininal entre 2 notes :"));
  for (int n = 0; n < N_NOTES; ++n) {
    if (Gap_Info_Tab[n].from_Oct < 0) {
      Serial.printf("%-6s -> %-6s : (aucun)\n",
        Gap_Info_Tab[n].from_Name, Gap_Info_Tab[n].to_Name);
    } else {
      Serial.printf("%-6s -> %-6s : %7.4f Hz  (oct %d -> %d)\n",
        Gap_Info_Tab[n].from_Name, Gap_Info_Tab[n].to_Name,
        Gap_Info_Tab[n].min_Hz,
        Gap_Info_Tab[n].from_Oct, Gap_Info_Tab[n].to_Oct);
    }
  }

}

void loop() {
  // put your main code here, to run repeatedly:

}
