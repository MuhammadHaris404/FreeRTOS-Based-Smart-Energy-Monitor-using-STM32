#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>

// ================= WIFI =================
#define WIFI_SSID     "Hotspot"
#define WIFI_PASSWORD "password"

// ================= FIREBASE =================
#define DATABASE_URL "https://power-monitoring-and-billing-default-rtdb.firebaseio.com/"
#define DATABASE_SECRET "24TVbWhqMmP3HZpSxzV9FDthDwpyqLpzFLBmDKvL"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= ENERGY PARAMETERS =================
float vrms = 0.0;
float irms = 0.0;
float power = 0.0;
float energy_kWh = 0.0;
float bill = 0.0;

float powerFactor = 0.9;
float costPerUnit = 6.5;

unsigned long lastCalcTime = 0;
unsigned long lastCloudUpdate = 0;

// ================= UART PARSER =================
bool parsePacket(String pkt)
{
  int v = pkt.indexOf("VRMS=");
  int i = pkt.indexOf("IRMS=");

  if (v < 0 || i < 0) return false;

  vrms = pkt.substring(v + 5, pkt.indexOf(",", v)).toFloat();
  irms = pkt.substring(i + 5, pkt.indexOf(">")).toFloat();

  return true;
}

// ================= ENERGY CALCULATION =================
void calculateEnergy()
{
  unsigned long now = millis();
  if (lastCalcTime == 0) {
    lastCalcTime = now;
    return;
  }

  float dt = (now - lastCalcTime) / 3600000.0;
  lastCalcTime = now;

  power = vrms * irms * powerFactor;
  energy_kWh += power * dt / 1000.0;
  bill = energy_kWh * costPerUnit;
}

// ================= CLOUD UPLOAD =================
void uploadToCloud()
{
  FirebaseJson json;

  json.set("voltage_V", vrms);
  json.set("current_A", irms);
  json.set("power_W", power);
  json.set("energy_kWh", energy_kWh);
  json.set("bill", bill);

  if (Firebase.setJSON(fbdo, "/energy_meter/live", json)) {
    Serial.println("Firebase upload OK");
  } else {
    Serial.print("Firebase error: ");
    Serial.println(fbdo.errorReason());
  }
}

// ================= SETUP =================
void setup()
{
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 16, 17);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Firebase using DATABASE SECRET (v4.4.17 compatible)
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("ESP32 Energy Monitor Ready");
}

// ================= LOOP =================
void loop()
{
  static String rx = "";

  while (Serial1.available()) {
    char c = Serial1.read();

    if (c == '<') rx = "";
    rx += c;

    if (c == '>') {
      if (parsePacket(rx)) {
        calculateEnergy();
      }
      rx = "";
    }
  }

  if (millis() - lastCloudUpdate > 5000) {
    uploadToCloud();
    lastCloudUpdate = millis();
  }
}
