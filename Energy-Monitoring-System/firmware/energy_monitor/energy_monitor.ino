/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  ENERGY MONITOR — FreeRTOS Dual-Core Edition                ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "EmonLib.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ─────────────────────────────────────────
// 1. CREDENTIALS
// ─────────────────────────────────────────
#define WIFI_SSID     "Amar"
#define WIFI_PASSWORD "Maitrimilyproject"
#define API_KEY       ""
#define DATABASE_URL  ""

// ─────────────────────────────────────────
// 2. PINS & CALIBRATION
// ─────────────────────────────────────────
#define VOLT_PIN       34
#define CURRENT_PIN_1  35
#define CURRENT_PIN_2  32
#define CURRENT_PIN_3  33
#define RELAY_1_PIN    27
#define RELAY_2_PIN     5
#define RELAY_3_PIN    13

const float VOLT_CAL      = 157.14;
const float CURRENT_CAL_1 = 0.30;
const float CURRENT_CAL_2 = 0.30;
const float CURRENT_CAL_3 = 0.30;
const float PHASE_SHIFT   = 1.7;

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

const int RELAY_PINS[3] = { RELAY_1_PIN, RELAY_2_PIN, RELAY_3_PIN };

// ─────────────────────────────────────────
// 3. TIMING
// ─────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS   2000
#define SENSOR_INTERVAL_MS      2000
#define RELAY_POLL_INTERVAL_MS   500
#define EMON_CROSSINGS            40
#define EMON_TIMEOUT_MS         2000

// ─────────────────────────────────────────
// 4. STACK SIZES
// ─────────────────────────────────────────
#define STACK_CORE0       8192
#define STACK_CORE1      16384
#define STACK_RELAY       8192

// ─────────────────────────────────────────
// 5. DEADBAND & OVERLOAD THRESHOLDS
// ─────────────────────────────────────────
const float  VOLT_THRESHOLD    = 0.5;
const float  AMP_THRESHOLD     = 0.001;
const float  POWER_THRESHOLD   = 1.0;
const double ENERGY_THRESHOLD  = 0.001;
const float  VOLT_ZERO_THRESH  = 25.0;

// Overload Limits
const float  GLOBAL_OVERLOAD_W = 1000.0;
const float  CURRENT_LIMIT_1   = 0.50;
const float  CURRENT_LIMIT_2   = 0.20;
const float  CURRENT_LIMIT_3   = 4.0;

// ─────────────────────────────────────────
// 6. DEVICE STATE
// ─────────────────────────────────────────
struct DeviceState {
  float  lastVoltage   = -100.0;
  float  lastCurrent   = -100.0;
  float  lastPower     = -100.0;
  double lastEnergy    = -1.0;
  bool   isInEmergency = false;
  bool   relayState    = true;
};

DeviceState devStates[3];
double energy_kWh[3] = { 0.0, 0.0, 0.0 };

// ─────────────────────────────────────────
// 7. FIREBASE
// ─────────────────────────────────────────
FirebaseData   fbdo0;
FirebaseData   fbdo1;
FirebaseData   fbdo2;
FirebaseAuth   auth;
FirebaseConfig config;

volatile bool signupOK = false;

// ─────────────────────────────────────────
// 8. EMON
// ─────────────────────────────────────────
EnergyMonitor emon1, emon2, emon3;

// ─────────────────────────────────────────
// 9. RTOS
// ─────────────────────────────────────────
SemaphoreHandle_t xFirebaseReady;
SemaphoreHandle_t xFirebaseReady2;

TaskHandle_t hTask0     = NULL;
TaskHandle_t hTask1     = NULL;
TaskHandle_t hTaskRelay = NULL;

// ─────────────────────────────────────────
// 10. PRE-BUILT PATH STRINGS
// ─────────────────────────────────────────
static char sPath[3][64];
static char sIsOn[3][40];
static char sEmerg[3][48];
static char rIsOn[3][40];

void buildPaths() {
  for (int i = 0; i < 3; i++) {
    int n = i + 1;
    snprintf(sIsOn[i],  sizeof(sIsOn[i]),  "Device/Device-%d/isOn", n);
    snprintf(sEmerg[i], sizeof(sEmerg[i]), "Device/Device-%d/Emergency%d", n, n);
    snprintf(rIsOn[i],  sizeof(rIsOn[i]),  "Device/Device-%d/isOn", n);
  }
}

static char _sensorPath[64];
const char* sensorPath(int index, const char* field, int num) {
  snprintf(_sensorPath, sizeof(_sensorPath),
           "Device/Device-%d/%s%d", index + 1, field, num);
  return _sensorPath;
}

// Internet check without ESP32Ping library
bool checkInternet() {
  IPAddress ip;
  return WiFi.hostByName("google.com", ip);
}

// ═════════════════════════════════════════
// CORE 0 TASK
// ═════════════════════════════════════════
void Task_Core0(void* pvParameters) {
  Serial.println("[C0] Task started");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[C0] Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(300));
    Serial.print(".");
  }
  Serial.println("\n[C0] Wi-Fi connected: " + WiFi.localIP().toString());

  // Internet check without extra library
  Serial.println("[C0] Checking Internet Access...");
  bool internet_ok = false;
  int retry_count = 0;

  while (!internet_ok && retry_count < 10) {
    if (checkInternet()) {
      internet_ok = true;
      Serial.println("[C0] Internet access confirmed!");
    } else {
      retry_count++;
      Serial.printf("[C0] No internet yet. Retrying (%d/10)...\n", retry_count);
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }

  if (!internet_ok) {
    Serial.println("[C0] WARNING: Connected to hotspot, but internet check failed.");
  }

  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("[C0] Firebase anonymous auth OK");
    signupOK = true;
  } else {
    Serial.printf("[C0] Auth failed: %s\n",
                  config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("[C0] Waiting for Firebase ready");
  unsigned long t0 = millis();
  while (!Firebase.ready() && millis() - t0 < 10000) {
    vTaskDelay(pdMS_TO_TICKS(300));
    Serial.print(".");
  }
  Serial.println();

  xSemaphoreGive(xFirebaseReady);
  xSemaphoreGive(xFirebaseReady2);
  Serial.println("[C0] Semaphores given — both Core 1 tasks unblocked");

  TickType_t xLastWake = xTaskGetTickCount();
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[C0] WiFi lost — reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      Serial.println(WiFi.status() == WL_CONNECTED
                     ? "[C0] WiFi reconnected" : "[C0] WiFi reconnect failed");
    }

    if (Firebase.ready() && signupOK) {
      if (!Firebase.RTDB.setTimestamp(&fbdo0, "Device/heartbeat")) {
        Serial.println("[C0] Heartbeat failed: " + fbdo0.errorReason());
      }
    }

    vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
  }
}

// ═════════════════════════════════════════
// CORE 1 HELPERS
// ═════════════════════════════════════════
void syncRelaysFromFirebase() {
  Serial.println("[C1] Syncing relay states...");
  for (int i = 0; i < 3; i++) {
    int  n      = i + 1;
    bool wantOn = true;

    if (Firebase.RTDB.getBool(&fbdo1, sIsOn[i])) {
      wantOn = fbdo1.boolData();
      Serial.printf("  [C1] Device-%d isOn=%s\n", n, wantOn ? "true" : "false");
    } else {
      Serial.printf("  [C1] Device-%d read err(%s) default ON\n",
                    n, fbdo1.errorReason().c_str());
      Firebase.RTDB.setBool(&fbdo1, sIsOn[i], true);
    }

    Firebase.RTDB.setBool(&fbdo1, sEmerg[i], false);

    devStates[i].relayState    = wantOn;
    devStates[i].isInEmergency = false;
    digitalWrite(RELAY_PINS[i], wantOn ? RELAY_ON : RELAY_OFF);
  }
  Serial.println("[C1] Sync complete.");
}

void processDevice(int index, int deviceNum,
                   float voltage, float current, float power,
                   double currentEnergy, bool isOverloaded) {
  int n = deviceNum;

  if (fabs(voltage - devStates[index].lastVoltage) >= VOLT_THRESHOLD) {
    Firebase.RTDB.setFloat(&fbdo1, sensorPath(index, "Voltage", n), voltage);
    devStates[index].lastVoltage = voltage;
  }
  if (fabs(current - devStates[index].lastCurrent) >= AMP_THRESHOLD) {
    Firebase.RTDB.setFloat(&fbdo1, sensorPath(index, "Current", n), current);
    devStates[index].lastCurrent = current;
  }
  if (fabs(power - devStates[index].lastPower) >= POWER_THRESHOLD) {
    Firebase.RTDB.setFloat(&fbdo1, sensorPath(index, "power", n), power);
    devStates[index].lastPower = power;
  }
  if (fabs(currentEnergy - devStates[index].lastEnergy) >= ENERGY_THRESHOLD) {
    Firebase.RTDB.setFloat(&fbdo1, sensorPath(index, "enCon", n), (float)currentEnergy);
    devStates[index].lastEnergy = currentEnergy;
  }

  if (isOverloaded) {
    if (!devStates[index].isInEmergency) {
      Firebase.RTDB.setBool(&fbdo1, sEmerg[index], true);
      Firebase.RTDB.setBool(&fbdo1, sIsOn[index], false);
      digitalWrite(RELAY_PINS[index], RELAY_OFF);
      devStates[index].isInEmergency = true;
      devStates[index].relayState    = false;
      Serial.printf("[C1] Device-%d EMERGENCY — relay OFF\n", n);
    }
  } else {
    if (devStates[index].isInEmergency) {
      if (Firebase.RTDB.getBool(&fbdo1, sEmerg[index]) && !fbdo1.boolData()) {
        devStates[index].isInEmergency = false;
        Serial.printf("[C1] Device-%d emergency cleared\n", n);
      }
    }
  }
}

// ═════════════════════════════════════════
// RELAY POLL TASK
// ═════════════════════════════════════════
void Task_RelayPoll(void* pvParameters) {
  Serial.println("[RP] Waiting for Firebase...");
  xSemaphoreTake(xFirebaseReady2, portMAX_DELAY);
  Serial.println("[RP] Active. Polling isOn every 500 ms.");

  for (int i = 0; i < 3; i++) {
    Serial.printf("[RP] Will poll: \"%s\"\n", rIsOn[i]);
  }

  TickType_t xLastWake = xTaskGetTickCount();

  for (;;) {
    if (!Firebase.ready() || !signupOK) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    for (int i = 0; i < 3; i++) {
      if (devStates[i].isInEmergency) {
        continue;
      }

      bool ok = Firebase.RTDB.getBool(&fbdo2, rIsOn[i]);

      if (ok) {
        bool wantOn = fbdo2.boolData();

        if (wantOn != devStates[i].relayState) {
          digitalWrite(RELAY_PINS[i], wantOn ? RELAY_ON : RELAY_OFF);
          devStates[i].relayState = wantOn;
          Serial.printf("[RP] >>> Device-%d relay toggled → %s <<<\n",
                        i + 1, wantOn ? "ON" : "OFF");
        }
      } else {
        Serial.printf("[RP] Device-%d getBool FAILED: %s\n",
                      i + 1, fbdo2.errorReason().c_str());
      }
    }

    vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(RELAY_POLL_INTERVAL_MS));
  }
}

// ═════════════════════════════════════════
// CORE 1 TASK — Sensors + uploads
// ═════════════════════════════════════════
void Task_Core1(void* pvParameters) {
  Serial.println("[C1] Waiting for Firebase...");
  xSemaphoreTake(xFirebaseReady, portMAX_DELAY);
  Serial.println("[C1] Unblocked. Starting sensor loop.");

  if (Firebase.ready() && signupOK) {
    syncRelaysFromFirebase();
  } else {
    for (int i = 0; i < 3; i++) {
      digitalWrite(RELAY_PINS[i], RELAY_ON);
      devStates[i].relayState = true;
    }
    Serial.println("[C1] Firebase not ready — relays defaulted ON");
  }

  unsigned long lastEnergyMs = millis();
  TickType_t xLastWake = xTaskGetTickCount();

  for (;;) {
    if (!Firebase.ready() || !signupOK) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    emon1.calcVI(EMON_CROSSINGS, EMON_TIMEOUT_MS);
    emon2.calcVI(EMON_CROSSINGS, EMON_TIMEOUT_MS);
    emon3.calcVI(EMON_CROSSINGS, EMON_TIMEOUT_MS);

    float Vrms  = emon1.Vrms      / 4.0f;
    float Irms1 = emon1.Irms      / 4.0f;
    float P1    = emon1.realPower / 16.0f;
    if (Irms1 < 0.04f) { Irms1 = 0.0f; P1 = 0.0f; }

    float Irms2 = emon2.Irms      / 4.0f;
    float P2    = emon2.realPower / 16.0f;
    if (Irms2 < 0.04f) { Irms2 = 0.0f; P2 = 0.0f; }

    float Irms3 = emon3.Irms      / 4.0f;
    float P3    = emon3.realPower / 16.0f;
    if (Irms3 < 0.01f) { Irms3 = 0.0f; P3 = 0.0f; }

    bool noSignal = (Vrms < VOLT_ZERO_THRESH);
    if (noSignal) {
      Serial.printf("[C1] Vrms=%.1fV < %.0fV — zeros\n", Vrms, VOLT_ZERO_THRESH);
      Vrms  = 0.0f;
      Irms1 = 0.0f; P1 = 0.0f;
      Irms2 = 0.0f; P2 = 0.0f;
      Irms3 = 0.0f; P3 = 0.0f;
      for (int i = 0; i < 3; i++) {
        devStates[i].lastVoltage = -100.0f;
        devStates[i].lastCurrent = -100.0f;
        devStates[i].lastPower   = -100.0f;
      }
    }

    // ── Enhanced Serial Monitor Display ──────────────────────────
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.printf( "║  ENERGY MONITOR  —  %lu ms               \n", millis());
    Serial.println("╠══════════════════════════════════════════╣");
    Serial.printf( "║  Voltage  :  %6.1f V                    \n", Vrms);
    Serial.println("╠══════════╦═══════════╦═══════════╦═══════╣");
    Serial.println("║ Channel  ║ Current   ║  Power    ║Energy ║");
    Serial.println("╠══════════╬═══════════╬═══════════╬═══════╣");
    Serial.printf( "║  Line 1  ║ %6.3f A  ║ %7.2f W ║ %.4f ║\n", Irms1, P1, energy_kWh[0]);
    Serial.printf( "║  Line 2  ║ %6.3f A  ║ %7.2f W ║ %.4f ║\n", Irms2, P2, energy_kWh[1]);
    Serial.printf( "║  Line 3  ║ %6.3f A  ║ %7.2f W ║ %.4f ║\n", Irms3, P3, energy_kWh[2]);
    Serial.println("╠══════════╩═══════════╬═══════════╩═══════╣");
    Serial.printf( "║  Total Power         ║ %7.2f W          \n", P1 + P2 + P3);
    Serial.println("╠══════════════════════╩══════════════════╣");
    Serial.printf( "║  Relay 1: %-4s  Relay 2: %-4s  Relay 3: %-4s\n",
                   devStates[0].relayState ? "ON " : "OFF",
                   devStates[1].relayState ? "ON " : "OFF",
                   devStates[2].relayState ? "ON " : "OFF");
    Serial.printf( "║  Emerg 1: %-3s   Emerg 2: %-3s   Emerg 3: %-3s \n",
                   devStates[0].isInEmergency ? "YES" : "NO ",
                   devStates[1].isInEmergency ? "YES" : "NO ",
                   devStates[2].isInEmergency ? "YES" : "NO ");
    Serial.println("╚══════════════════════════════════════════╝");
    // ─────────────────────────────────────────────────────────────

    unsigned long now = millis();
    double deltaH = (double)(now - lastEnergyMs) / 3600000.0;
    lastEnergyMs = now;

    if (!noSignal) {
      energy_kWh[0] += (P1 / 1000.0) * deltaH;
      energy_kWh[1] += (P2 / 1000.0) * deltaH;
      energy_kWh[2] += (P3 / 1000.0) * deltaH;
    }

    bool globalOverload = (P1 + P2 + P3) > GLOBAL_OVERLOAD_W;
    if (globalOverload) Serial.println("[C1] !!! GLOBAL POWER OVERLOAD !!!");

    bool overload1 = globalOverload || (Irms1 > CURRENT_LIMIT_1);
    bool overload2 = globalOverload || (Irms2 > CURRENT_LIMIT_2);
    bool overload3 = globalOverload || (Irms3 > CURRENT_LIMIT_3);

    if (Irms1 > CURRENT_LIMIT_1) Serial.printf("[C1] !!! L1 OVERLOAD: %.2fA > %.2fA !!!\n", Irms1, CURRENT_LIMIT_1);
    if (Irms2 > CURRENT_LIMIT_2) Serial.printf("[C1] !!! L2 OVERLOAD: %.2fA > %.2fA !!!\n", Irms2, CURRENT_LIMIT_2);
    if (Irms3 > CURRENT_LIMIT_3) Serial.printf("[C1] !!! L3 OVERLOAD: %.3fA > %.2fA !!!\n", Irms3, CURRENT_LIMIT_3);

    processDevice(0, 1, Vrms, Irms1, P1, energy_kWh[0], overload1);
    processDevice(1, 2, Vrms, Irms2, P2, energy_kWh[1], overload2);
    processDevice(2, 3, Vrms, Irms3, P3, energy_kWh[2], overload3);

    vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
  }
}

// ═════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);

  buildPaths();

  for (int i = 0; i < 3; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
  }

  emon1.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon1.current(CURRENT_PIN_1, CURRENT_CAL_1);
  emon2.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon2.current(CURRENT_PIN_2, CURRENT_CAL_2);
  emon3.voltage(VOLT_PIN, VOLT_CAL, PHASE_SHIFT);
  emon3.current(CURRENT_PIN_3, CURRENT_CAL_3);

  xFirebaseReady  = xSemaphoreCreateBinary();
  xFirebaseReady2 = xSemaphoreCreateBinary();

  xTaskCreatePinnedToCore(Task_Core0,     "WiFi_Heartbeat", STACK_CORE0, NULL, 2, &hTask0,     0);
  vTaskDelay(pdMS_TO_TICKS(100));
  xTaskCreatePinnedToCore(Task_Core1,     "Sensor_Upload",  STACK_CORE1, NULL, 1, &hTask1,     1);
  vTaskDelay(pdMS_TO_TICKS(100));
  xTaskCreatePinnedToCore(Task_RelayPoll, "Relay_Poll",     STACK_RELAY, NULL, 1, &hTaskRelay, 1);

  vTaskDelete(NULL);
}

void loop() {}