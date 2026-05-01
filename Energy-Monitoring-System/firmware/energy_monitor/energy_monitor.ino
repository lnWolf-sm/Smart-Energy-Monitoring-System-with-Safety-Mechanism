#include "EmonLib.h"

#define VOLT_PIN 34
#define CURRENT_PIN_1 35
#define CURRENT_PIN_2 32
#define CURRENT_PIN_3 33  
#define RELAY_1_PIN 27    
#define RELAY_2_PIN 5     
#define RELAY_3_PIN 13    

// --- CALIBRATION PARAMETERS ---
const float VOLT_CAL = 157.14;   
const float CURRENT_CAL_1 = 0.30; 
const float CURRENT_CAL_2 = 0.30; 
const float CURRENT_CAL_3 = 0.30; 
const float PHASE_SHIFT = 1.7; 

// --- RELAY LOGIC ---
#define RELAY_ON LOW
#define RELAY_OFF HIGH

EnergyMonitor emon1, emon2, emon3;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); 
  
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  digitalWrite(RELAY_1_PIN, RELAY_OFF); 
  digitalWrite(RELAY_2_PIN, RELAY_OFF); 
  digitalWrite(RELAY_3_PIN, RELAY_OFF); 
  
  emon1.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon1.current(CURRENT_PIN_1, CURRENT_CAL_1);
  
  emon2.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon2.current(CURRENT_PIN_2, CURRENT_CAL_2);
  
  emon3.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon3.current(CURRENT_PIN_3, CURRENT_CAL_3);
  
  Serial.println("ESP32 Energy Monitor - Low Current Detection Mode");
  delay(1000);
  digitalWrite(RELAY_1_PIN, RELAY_ON);
  digitalWrite(RELAY_2_PIN, RELAY_ON);
  digitalWrite(RELAY_3_PIN, RELAY_ON);
}

void loop() {
  // Increased samples for better averaging of small signals
  emon1.calcVI(40, 2000);
  emon2.calcVI(40, 2000);
  emon3.calcVI(40, 2000);
  
  float Vrms = emon1.Vrms / 4.0;
  
  // --- PROCESSING LOADS ---
  // Load 1 & 2 (Standard)
  float Irms1 = emon1.Irms / 4.0;
  float P_real1 = emon1.realPower / 16.0;
  if (Irms1 < 0.04) { Irms1 = 0; P_real1 = 0; }
  
  float Irms2 = emon2.Irms / 4.0;
  float P_real2 = emon2.realPower / 16.0;
  if (Irms2 < 0.04) { Irms2 = 0; P_real2 = 0; }

  // Load 3 (Optimized for Charger)
  // We lower the noise floor significantly for Load 3
  float Irms3 = emon3.Irms / 4.0;
  float P_real3 = emon3.realPower / 16.0;
  
  // A phone charger at 220V draws ~0.05A to 0.1A. 
  // We set the noise floor to 0.01A to catch it.
  if (Irms3 < 0.01) { 
    Irms3 = 0; 
    P_real3 = 0; 
  }

  // --- SERIAL OUTPUT ---
  Serial.print("V: "); Serial.print(Vrms, 1); Serial.print("V | ");
  
  Serial.print("L1: "); Serial.print(Irms1, 2); Serial.print("A (");
  Serial.print(P_real1, 1); Serial.print("W) | ");
  
  Serial.print("L2: "); Serial.print(Irms2, 2); Serial.print("A (");
  Serial.print(P_real2, 1); Serial.print("W) | ");
  
  // Show 3 decimal places for Load 3 to see the small charger current
  Serial.print("L3: "); Serial.print(Irms3, 3); Serial.print("A (");
  Serial.print(P_real3, 1); Serial.println("W)");
  
  // --- PROTECTION ---
  if ((P_real1 + P_real2 + P_real3) > 1000.0) { 
    digitalWrite(RELAY_1_PIN, RELAY_OFF);
    digitalWrite(RELAY_2_PIN, RELAY_OFF);
    digitalWrite(RELAY_3_PIN, RELAY_OFF);
    Serial.println("!!! OVERLOAD !!!");
    while(1);
  }
  
  delay(2000); 
}